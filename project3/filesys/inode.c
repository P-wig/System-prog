#include <debug.h>
#include <round.h>
#include <string.h>

#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* Identifies an inode. */
#define INODE_MAGIC 0x494e4f44

/* Returns the number of sectors to allocate for an inode SIZE
 * bytes long. */
static inline size_t
bytes_to_sectors(off_t size)
{
    return DIV_ROUND_UP(size, BLOCK_SECTOR_SIZE);
}

/* An indirect block: just an array of sector numbers that fills
 * exactly one disk sector. Used both for the inode's single
 * indirect block and for each of the 128 indirect blocks pointed
 * to by the doubly-indirect block. */
struct indirect_block {
    block_sector_t sectors[INDIRECT_CNT];
};

/* Reads indirect block INDIRECT_SECTOR off disk and returns the
 * sector number stored at INDEX within it.
 * Returns 0 (never a valid data sector) if INDIRECT_SECTOR is 0,
 * i.e. the indirect block itself hasn't been allocated yet. */
static block_sector_t
index_block_sector(block_sector_t indirect_sector, size_t index)
{
    struct indirect_block ib;

    if (indirect_sector == 0) {
        return 0;
    }
    block_read(fs_device, indirect_sector, &ib);
    return ib.sectors[index];
}

/* Returns the block device sector that contains byte offset POS
 * within INODE.
 * Returns -1 if INODE does not contain data for a byte at offset
 * POS. */
static block_sector_t
byte_to_sector(const struct inode *inode, off_t pos)
{
    size_t index;

    ASSERT(inode != NULL);
    if (pos >= inode->data.length) {
        return -1;
    }

    index = pos / BLOCK_SECTOR_SIZE;

    /* First 124 sectors come straight out of the inode itself. */
    if (index < DIRECT_CNT) {
        return inode->data.direct[index];
    }
    index -= DIRECT_CNT;

    /* Next 128 sectors are listed in the one indirect block. */
    if (index < INDIRECT_CNT) {
        return index_block_sector(inode->data.indirect, index);
    }
    index -= INDIRECT_CNT;

    /* Remaining sectors go through the doubly-indirect block: it
     * points to up to 128 indirect blocks, each listing 128 data
     * sectors. */
    ASSERT(index < (size_t) INDIRECT_CNT * INDIRECT_CNT);
    {
        size_t first_level = index / INDIRECT_CNT;
        size_t second_level = index % INDIRECT_CNT;
        block_sector_t indirect_sector =
            index_block_sector(inode->data.double_indirect, first_level);
        return index_block_sector(indirect_sector, second_level);
    }
}

/* Returns the data sector at INDEX within the indirect block whose
 * sector number is stored in *INDIRECT_SECTORP, allocating that
 * data sector -- and the indirect block itself, if *INDIRECT_SECTORP
 * is still 0 -- and zero-filling whatever is newly allocated.
 * Writes the (possibly just-created) indirect block back to disk
 * with the new entry in place, and stores a freshly allocated
 * indirect block's sector number back into *INDIRECT_SECTORP.
 * Returns 0 if a disk-full allocation failure occurs at any point. */
static block_sector_t
get_or_alloc_through_indirect(block_sector_t *indirect_sectorp, size_t index)
{
    static char zeros[BLOCK_SECTOR_SIZE];
    struct indirect_block ib;
    bool is_new_block = (*indirect_sectorp == 0);

    if (is_new_block) {
        if (!free_map_allocate(1, indirect_sectorp)) {
            return 0;
        }
        memset(&ib, 0, sizeof ib);
    } else {
        block_read(fs_device, *indirect_sectorp, &ib);
    }

    if (ib.sectors[index] == 0) {
        if (!free_map_allocate(1, &ib.sectors[index])) {
            if (is_new_block) {
                /* Persist the (empty) new indirect block anyway, so
                 * *INDIRECT_SECTORP points at valid, zeroed data. */
                block_write(fs_device, *indirect_sectorp, &ib);
            }
            return 0;
        }
        block_write(fs_device, ib.sectors[index], zeros);
    }
    block_write(fs_device, *indirect_sectorp, &ib);
    return ib.sectors[index];
}

/* Ensures that DISK_INODE has at least SECTOR_CNT data sectors
 * allocated -- across its direct, indirect, and doubly-indirect
 * pointers, in that order -- zero-filling any sector allocated here
 * for the first time. Sectors already allocated (e.g. by an earlier
 * call, when growing a file) are left untouched.
 * Returns true if successful, false on disk-full. */
static bool
inode_allocate(struct inode_disk *disk_inode, size_t sector_cnt)
{
    static char zeros[BLOCK_SECTOR_SIZE];
    size_t i;

    for (i = 0; i < sector_cnt; i++) {
        if (i < DIRECT_CNT) {
            if (disk_inode->direct[i] == 0) {
                if (!free_map_allocate(1, &disk_inode->direct[i])) {
                    return false;
                }
                block_write(fs_device, disk_inode->direct[i], zeros);
            }
        } else if (i - DIRECT_CNT < INDIRECT_CNT) {
            size_t idx = i - DIRECT_CNT;
            if (get_or_alloc_through_indirect(&disk_inode->indirect, idx)
                == 0) {
                return false;
            }
        } else {
            size_t idx = i - DIRECT_CNT - INDIRECT_CNT;
            size_t first_level = idx / INDIRECT_CNT;
            size_t second_level = idx % INDIRECT_CNT;
            block_sector_t l1_sector = get_or_alloc_through_indirect(
                &disk_inode->double_indirect, first_level);
            if (l1_sector == 0
                || get_or_alloc_through_indirect(&l1_sector, second_level)
                       == 0) {
                return false;
            }
        }
    }
    return true;
}

/* Releases every sector listed in indirect block INDIRECT_SECTOR,
 * the first CNT of its entries, then releases the indirect block's
 * own sector. */
static void
free_indirect_block(block_sector_t indirect_sector, size_t cnt)
{
    struct indirect_block ib;
    size_t i;

    block_read(fs_device, indirect_sector, &ib);
    for (i = 0; i < cnt; i++) {
        free_map_release(ib.sectors[i], 1);
    }
    free_map_release(indirect_sector, 1);
}

/* Releases every data sector, indirect block, and doubly-indirect
 * block belonging to DISK_INODE, based on its current LENGTH. */
static void
inode_deallocate(struct inode_disk *disk_inode)
{
    size_t sectors = bytes_to_sectors(disk_inode->length);
    size_t direct_cnt = sectors < DIRECT_CNT ? sectors : DIRECT_CNT;
    size_t i;

    for (i = 0; i < direct_cnt; i++) {
        free_map_release(disk_inode->direct[i], 1);
    }
    sectors -= direct_cnt;
    if (sectors == 0) {
        return;
    }

    {
        size_t indirect_cnt = sectors < INDIRECT_CNT ? sectors : INDIRECT_CNT;
        free_indirect_block(disk_inode->indirect, indirect_cnt);
        sectors -= indirect_cnt;
    }
    if (sectors == 0) {
        return;
    }

    {
        struct indirect_block dib;
        size_t l1_blocks = DIV_ROUND_UP(sectors, INDIRECT_CNT);
        size_t i;

        block_read(fs_device, disk_inode->double_indirect, &dib);
        for (i = 0; i < l1_blocks; i++) {
            size_t cnt = sectors < INDIRECT_CNT ? sectors : INDIRECT_CNT;
            free_indirect_block(dib.sectors[i], cnt);
            sectors -= cnt;
        }
        free_map_release(disk_inode->double_indirect, 1);
    }
}

/* List of open inodes, so that opening a single inode twice
 * returns the same `struct inode'. */
static struct list open_inodes;

/* Initializes the inode module. */
void
inode_init(void)
{
    list_init(&open_inodes);
}

/* Initializes an inode with LENGTH bytes of data and
 * writes the new inode to sector SECTOR on the file system
 * device.
 * Returns true if successful.
 * Returns false if memory or disk allocation fails. */
bool
inode_create(block_sector_t sector, off_t length)
{
    struct inode_disk *disk_inode = NULL;
    bool success = false;

    ASSERT(length >= 0);

    /* If this assertion fails, the inode structure is not exactly
     * one sector in size, and you should fix that. */
    ASSERT(sizeof *disk_inode == BLOCK_SECTOR_SIZE);

    disk_inode = calloc(1, sizeof *disk_inode);
    if (disk_inode != NULL) {
        size_t sectors = bytes_to_sectors(length);
        disk_inode->length = length;
        disk_inode->magic = INODE_MAGIC;
        if (inode_allocate(disk_inode, sectors)) {
            /* Write the inode itself last, now that inode_allocate()
             * has filled in its direct/indirect/doubly-indirect
             * pointers. */
            block_write(fs_device, sector, disk_inode);
            success = true;
        }
        free(disk_inode);
    }
    return success;
}

/* Reads an inode from SECTOR
 * and returns a `struct inode' that contains it.
 * Returns a null pointer if memory allocation fails. */
struct inode *
inode_open(block_sector_t sector)
{
    struct list_elem *e;
    struct inode *inode;

    /* Check whether this inode is already open. */
    for (e = list_begin(&open_inodes); e != list_end(&open_inodes);
         e = list_next(e)) {
        inode = list_entry(e, struct inode, elem);
        if (inode->sector == sector) {
            inode_reopen(inode);
            return inode;
        }
    }

    /* Allocate memory. */
    inode = malloc(sizeof *inode);
    if (inode == NULL) {
        return NULL;
    }

    /* Initialize. */
    list_push_front(&open_inodes, &inode->elem);
    inode->sector = sector;
    inode->open_cnt = 1;
    inode->deny_write_cnt = 0;
    inode->removed = false;
    block_read(fs_device, inode->sector, &inode->data);
    return inode;
}

/* Reopens and returns INODE. */
struct inode *
inode_reopen(struct inode *inode)
{
    if (inode != NULL) {
        inode->open_cnt++;
    }
    return inode;
}

/* Returns INODE's inode number. */
block_sector_t
inode_get_inumber(const struct inode *inode)
{
    return inode->sector;
}

/* Closes INODE and writes it to disk.
 * If this was the last reference to INODE, frees its memory.
 * If INODE was also a removed inode, frees its blocks. */
void
inode_close(struct inode *inode)
{
    /* Ignore null pointer. */
    if (inode == NULL) {
        return;
    }

    /* Release resources if this was the last opener. */
    if (--inode->open_cnt == 0) {
        /* Remove from inode list and release lock. */
        list_remove(&inode->elem);

        /* Deallocate blocks if removed. */
        if (inode->removed) {
            free_map_release(inode->sector, 1);
            inode_deallocate(&inode->data);
        }

        free(inode);
    }
}

/* Marks INODE to be deleted when it is closed by the last caller who
 * has it open. */
void
inode_remove(struct inode *inode)
{
    ASSERT(inode != NULL);
    inode->removed = true;
}

/* Reads SIZE bytes from INODE into BUFFER, starting at position OFFSET.
 * Returns the number of bytes actually read, which may be less
 * than SIZE if an error occurs or end of file is reached. */
off_t
inode_read_at(struct inode *inode, void *buffer_, off_t size, off_t offset)
{
    uint8_t *buffer = buffer_;
    off_t bytes_read = 0;
    uint8_t *bounce = NULL;

    while (size > 0) {
        /* Disk sector to read, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually copy out of this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0) {
            break;
        }

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
            /* Read full sector directly into caller's buffer. */
            block_read(fs_device, sector_idx, buffer + bytes_read);
        } else {
            /* Read sector into bounce buffer, then partially copy
             * into caller's buffer. */
            if (bounce == NULL) {
                bounce = malloc(BLOCK_SECTOR_SIZE);
                if (bounce == NULL) {
                    break;
                }
            }
            block_read(fs_device, sector_idx, bounce);
            memcpy(buffer + bytes_read, bounce + sector_ofs, chunk_size);
        }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_read += chunk_size;
    }
    free(bounce);

    return bytes_read;
}

/* Writes SIZE bytes from BUFFER into INODE, starting at OFFSET.
 * Returns the number of bytes actually written, which may be
 * less than SIZE if an error occurs.
 * A write that extends past EOF grows the file to exactly cover
 * it; see the growth step below for how the gap between the old
 * EOF and OFFSET ends up zeroed. */
off_t
inode_write_at(struct inode *inode, const void *buffer_, off_t size,
               off_t offset)
{
    const uint8_t *buffer = buffer_;
    off_t bytes_written = 0;
    uint8_t *bounce = NULL;

    if (inode->deny_write_cnt) {
        return 0;
    }

    /* Grow the file if this write reaches past the current EOF.
     * inode_allocate() only allocates sectors that aren't already
     * there and zero-fills each one as it's allocated -- including
     * both brand-new sectors past the old EOF and any unwritten tail
     * of a sector that was already allocated. That's what makes any
     * gap between the old EOF and OFFSET read back as zeros, with no
     * extra zero-filling code needed here. */
    if (size > 0 && offset + size > inode->data.length) {
        off_t new_length = offset + size;
        if (!inode_allocate(&inode->data, bytes_to_sectors(new_length))) {
            return 0; /* Disk full: can't grow, so nothing can be written. */
        }
        inode->data.length = new_length;
        block_write(fs_device, inode->sector, &inode->data);
    }

    while (size > 0) {
        /* Sector to write, starting byte offset within sector. */
        block_sector_t sector_idx = byte_to_sector(inode, offset);
        int sector_ofs = offset % BLOCK_SECTOR_SIZE;

        /* Bytes left in inode, bytes left in sector, lesser of the two. */
        off_t inode_left = inode_length(inode) - offset;
        int sector_left = BLOCK_SECTOR_SIZE - sector_ofs;
        int min_left = inode_left < sector_left ? inode_left : sector_left;

        /* Number of bytes to actually write into this sector. */
        int chunk_size = size < min_left ? size : min_left;
        if (chunk_size <= 0) {
            break;
        }

        if (sector_ofs == 0 && chunk_size == BLOCK_SECTOR_SIZE) {
            /* Write full sector directly to disk. */
            block_write(fs_device, sector_idx, buffer + bytes_written);
        } else {
            /* We need a bounce buffer. */
            if (bounce == NULL) {
                bounce = malloc(BLOCK_SECTOR_SIZE);
                if (bounce == NULL) {
                    break;
                }
            }

            /* If the sector contains data before or after the chunk
             * we're writing, then we need to read in the sector
             * first.  Otherwise we start with a sector of all zeros. */
            if (sector_ofs > 0 || chunk_size < sector_left) {
                block_read(fs_device, sector_idx, bounce);
            } else {
                memset(bounce, 0, BLOCK_SECTOR_SIZE);
            }
            memcpy(bounce + sector_ofs, buffer + bytes_written, chunk_size);
            block_write(fs_device, sector_idx, bounce);
        }

        /* Advance. */
        size -= chunk_size;
        offset += chunk_size;
        bytes_written += chunk_size;
    }
    free(bounce);

    return bytes_written;
}

/* Disables writes to INODE.
 * May be called at most once per inode opener. */
void
inode_deny_write(struct inode *inode)
{
    inode->deny_write_cnt++;
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
}

/* Re-enables writes to INODE.
 * Must be called once by each inode opener who has called
 * inode_deny_write() on the inode, before closing the inode. */
void
inode_allow_write(struct inode *inode)
{
    ASSERT(inode->deny_write_cnt > 0);
    ASSERT(inode->deny_write_cnt <= inode->open_cnt);
    inode->deny_write_cnt--;
}

/* Returns the length, in bytes, of INODE's data. */
off_t
inode_length(const struct inode *inode)
{
    return inode->data.length;
}
