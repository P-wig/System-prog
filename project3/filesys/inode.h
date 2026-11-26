#ifndef FILESYS_INODE_H
#define FILESYS_INODE_H

#include <list.h>
#include <stdbool.h>

#include "devices/block.h"
#include "filesys/off_t.h"

struct bitmap;

/* Index layout: direct, singly-indirect, and doubly-indirect pointers.
 *
 * A sector holds BLOCK_SECTOR_SIZE / 4 = 128 sector numbers, so:
 *   direct:          123 sectors                  (~61.5 KB)
 *   indirect:        128 sectors                  (~64 KB)
 *   doubly-indirect: 128 * 128 = 16384 sectors    (~8 MB)
 * Max file size is 16635 sectors, just over the 8 MB partition.
 * (One direct slot was given up, versus the original 124, to make
 * room for the is_dir flag below while keeping the struct exactly
 * one sector.) */
#define DIRECT_CNT   123
#define INDIRECT_CNT 128 /* Sector numbers per indirect block. */

/* On-disk inode.
 * Must be exactly BLOCK_SECTOR_SIZE bytes long.
 * A sector number of 0 in any pointer means "not allocated" (sector 0
 * holds the free map, so it is never a data sector). */
struct inode_disk {
    off_t          length;                /* File size in bytes. */
    unsigned       magic;                 /* Magic number. */
    bool           is_dir;                /* True if this inode is a directory. */
    block_sector_t direct[DIRECT_CNT];    /* Direct data sectors. */
    block_sector_t indirect;              /* Sector of an indirect block. */
    block_sector_t double_indirect;       /* Sector of a doubly-indirect block. */
};

/* In-memory inode. */
struct inode {
    struct list_elem  elem;           /* Element in inode list. */
    block_sector_t    sector;         /* Sector number of disk location. */
    int               open_cnt;       /* Number of openers. */
    bool              removed;        /* True if deleted, false otherwise. */
    int               deny_write_cnt; /* 0: writes ok, >0: deny writes. */
    struct inode_disk data;           /* Inode content. */
};

void inode_init(void);
bool inode_create(block_sector_t, off_t, bool is_dir);
struct inode *inode_open(block_sector_t);
struct inode *inode_reopen(struct inode *);
block_sector_t inode_get_inumber(const struct inode *);
void inode_close(struct inode *);
void inode_remove(struct inode *);
off_t inode_read_at(struct inode *, void *, off_t size, off_t offset);
off_t inode_write_at(struct inode *, const void *, off_t size, off_t offset);
void inode_deny_write(struct inode *);
void inode_allow_write(struct inode *);
off_t inode_length(const struct inode *);
bool inode_is_dir(const struct inode *);
int inode_open_cnt(const struct inode *);

#endif /* filesys/inode.h */
