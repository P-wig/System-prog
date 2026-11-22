#include <debug.h>
#include <stdio.h>
#include <string.h>

#include "filesys/directory.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "filesys/free-map.h"
#include "filesys/inode.h"
#include "threads/malloc.h"

/* Partition that contains the file system. */
struct block *fs_device;

static void do_format(void);

/* Initializes the file system module.
 * If FORMAT is true, reformats the file system. */
void
filesys_init(bool format)
{
    fs_device = block_get_role(BLOCK_FILESYS);
    if (fs_device == NULL) {
        PANIC("No file system device found, can't initialize file system.");
    }

    inode_init();
    free_map_init();

    if (format) {
        do_format();
    }

    free_map_open();
}

/* Shuts down the file system module, writing any unwritten data
 * to disk. */
void
filesys_done(void)
{
    free_map_close();
}

/* Returns the directory that a path should start resolving from:
 * the root for an absolute PATH (one starting with '/'), or the
 * calling thread's current working directory for a relative one.
 * The caller must dir_close() the result.
 *
 * TODO: once a per-process cwd field exists on struct thread,
 * relative paths should start from thread_current()->cwd instead
 * of always falling back to the root. Until then, there's no
 * other directory a relative path could mean anyway, since nothing
 * can enter a non-root directory without that field. */
static struct dir *
resolve_start_dir(const char *path)
{
    if (path[0] == '/') {
        return dir_open_root();
    }
    return dir_open_root();
}

/* Resolves PATH -- absolute (leading '/') or relative to the
 * calling process's current directory -- to the directory that
 * contains (or should contain) its final path component, and
 * copies that final component's name into NAME (which must have
 * room for NAME_MAX + 1 bytes). It does NOT check whether that
 * final component actually exists in the returned directory --
 * that's for the caller to decide, since different callers want
 * different things (filesys_open() wants it to exist,
 * filesys_create() wants it not to, and so on).
 *
 * Repeated slashes are collapsed ("a//b" behaves like "a/b") and
 * "." / ".." path components are not special-cased here at all:
 * every directory already contains real "." and ".." entries
 * pointing at itself and its parent (see dir_create()), including
 * the root, whose ".." entry points back at the root itself. So an
 * intermediate "." or ".." component is resolved by the same plain
 * dir_lookup() as any other name, and "/../x" naturally ends up
 * the same place as "/x" with no extra code.
 *
 * Returns NULL if PATH is empty, or if any component before the
 * last fails to name an existing directory (including the case
 * where it names an existing file instead of a directory).
 *
 * If PATH consists of nothing but slashes (e.g. "/"), there is no
 * final component to split off: the resolved directory is the
 * root itself, so this returns the root and sets NAME to ".",
 * letting a caller's subsequent dir_lookup(result, name, ...) find
 * the root's own inode via its self-referencing "." entry. */
struct dir *
filesys_resolve_path(const char *path, char name[NAME_MAX + 1])
{
    struct dir *dir;
    char *path_copy, *token, *next_token, *save_ptr;
    bool found_component = false;

    if (path == NULL || path[0] == '\0') {
        return NULL;
    }

    dir = resolve_start_dir(path);
    if (dir == NULL) {
        return NULL;
    }

    path_copy = malloc(strlen(path) + 1);
    if (path_copy == NULL) {
        dir_close(dir);
        return NULL;
    }
    strlcpy(path_copy, path, strlen(path) + 1);

    /* Walk PATH one '/'-separated component at a time, always
     * holding one component back so that when input runs out, DIR
     * is left open on that component's parent, not the component
     * itself. */
    token = strtok_r(path_copy, "/", &save_ptr);
    while (token != NULL) {
        struct inode *inode;
        struct dir *next_dir;

        next_token = strtok_r(NULL, "/", &save_ptr);
        if (next_token == NULL) {
            /* TOKEN is the final component: stop here and leave it
             * for the caller to look up (or create). */
            strlcpy(name, token, NAME_MAX + 1);
            found_component = true;
            break;
        }

        if (!dir_lookup(dir, token, &inode) || !inode_is_dir(inode)) {
            if (inode != NULL) {
                inode_close(inode);
            }
            dir_close(dir);
            free(path_copy);
            return NULL;
        }
        next_dir = dir_open(inode); /* Takes ownership of INODE. */
        if (next_dir == NULL) {
            dir_close(dir);
            free(path_copy);
            return NULL;
        }
        dir_close(dir);
        dir = next_dir;

        token = next_token;
    }
    free(path_copy);

    if (!found_component) {
        strlcpy(name, ".", NAME_MAX + 1);
    }

    return dir;
}

/* Creates a file named NAME with the given INITIAL_SIZE.
 * Returns true if successful, false otherwise.
 * Fails if a file named NAME already exists,
 * or if internal memory allocation fails. */
bool
filesys_create(const char *name, off_t initial_size)
{
    block_sector_t inode_sector = 0;
    struct dir *dir = dir_open_root();
    bool success = (dir != NULL
                    && free_map_allocate(1, &inode_sector)
                    && inode_create(inode_sector, initial_size, false)
                    && dir_add(dir, name, inode_sector));

    if (!success && inode_sector != 0) {
        free_map_release(inode_sector, 1);
    }
    dir_close(dir);

    return success;
}

/* Opens the file with the given NAME.
 * Returns the new file if successful or a null pointer
 * otherwise.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
struct file *
filesys_open(const char *name)
{
    struct dir *dir = dir_open_root();
    struct inode *inode = NULL;

    if (dir != NULL) {
        dir_lookup(dir, name, &inode);
    }
    dir_close(dir);

    return file_open(inode);
}

/* Deletes the file named NAME.
 * Returns true if successful, false on failure.
 * Fails if no file named NAME exists,
 * or if an internal memory allocation fails. */
bool
filesys_remove(const char *name)
{
    struct dir *dir = dir_open_root();
    bool success = dir != NULL && dir_remove(dir, name);

    dir_close(dir);

    return success;
}

/* Formats the file system. */
static void
do_format(void)
{
    printf("Formatting file system...");
    free_map_create();
    /* The root directory is its own parent: by convention, ".."
     * from the root resolves back to the root itself. */
    if (!dir_create(ROOT_DIR_SECTOR, ROOT_DIR_SECTOR, 16)) {
        PANIC("root directory creation failed");
    }
    free_map_close();
    printf("done.\n");
}
