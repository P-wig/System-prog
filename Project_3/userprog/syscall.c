#include <debug.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <syscall-nr.h>

#include "devices/input.h"
#include "devices/shutdown.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "threads/interrupt.h"
#include "threads/malloc.h"
#include "threads/synch.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/pagedir.h"
#include "userprog/process.h"
#include "userprog/syscall.h"

/* Reserved file descriptors; fd 0/1 never appear in a thread's
 * fd_list. */
#define STDIN_FILENO  0
#define STDOUT_FILENO 1

/* One file a process currently has open, mapping its
 * process-local file descriptor to the underlying kernel file. */
struct fd_entry {
    int              fd;
    struct file     *file;
    struct list_elem elem;
};

/* Guards every filesys_*()/file_*() call.  The filesystem
 * implementation isn't reentrant, so at most one syscall may be
 * using it at a time, even across different processes. */
static struct lock filesys_lock;

static void syscall_handler(struct intr_frame *);

static void     check_user_range(const void *uaddr, size_t size);
static void     check_user_string(const char *ustr);
static uint32_t arg_word(struct intr_frame *f, int idx);

static void sys_exit(int status) NO_RETURN;
static tid_t sys_exec(const char *cmd_line);
static int sys_wait(tid_t pid);
static bool sys_create(const char *file, unsigned initial_size);
static bool sys_remove(const char *file);
static int sys_open(const char *file);
static int sys_filesize(int fd);
static int sys_read(int fd, void *buffer, unsigned length);
static int sys_write(int fd, const void *buffer, unsigned length);
static void sys_seek(int fd, unsigned position);
static unsigned sys_tell(int fd);
static void sys_close(int fd);

static struct fd_entry *lookup_fd(int fd);

void
syscall_init(void)
{
    lock_init(&filesys_lock);
    intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler(struct intr_frame *f)
{
    uint32_t syscall_no = arg_word(f, 0);

    switch (syscall_no) {
    case SYS_HALT:
        shutdown_power_off();
        NOT_REACHED();

    case SYS_EXIT:
        sys_exit((int)arg_word(f, 1));
        NOT_REACHED();

    case SYS_EXEC:
        f->eax = (uint32_t)sys_exec((const char *)arg_word(f, 1));
        break;

    case SYS_WAIT:
        f->eax = (uint32_t)sys_wait((tid_t)arg_word(f, 1));
        break;

    case SYS_CREATE:
        f->eax = (uint32_t)sys_create((const char *)arg_word(f, 1),
                                       (unsigned)arg_word(f, 2));
        break;

    case SYS_REMOVE:
        f->eax = (uint32_t)sys_remove((const char *)arg_word(f, 1));
        break;

    case SYS_OPEN:
        f->eax = (uint32_t)sys_open((const char *)arg_word(f, 1));
        break;

    case SYS_FILESIZE:
        f->eax = (uint32_t)sys_filesize((int)arg_word(f, 1));
        break;

    case SYS_READ:
        f->eax = (uint32_t)sys_read((int)arg_word(f, 1),
                                     (void *)arg_word(f, 2),
                                     (unsigned)arg_word(f, 3));
        break;

    case SYS_WRITE:
        f->eax = (uint32_t)sys_write((int)arg_word(f, 1),
                                      (const void *)arg_word(f, 2),
                                      (unsigned)arg_word(f, 3));
        break;

    case SYS_SEEK:
        sys_seek((int)arg_word(f, 1), (unsigned)arg_word(f, 2));
        break;

    case SYS_TELL:
        f->eax = (uint32_t)sys_tell((int)arg_word(f, 1));
        break;

    case SYS_CLOSE:
        sys_close((int)arg_word(f, 1));
        break;

    case SYS_MMAP:
    case SYS_MUNMAP:
        /* Memory-mapped files belong to the virtual-memory module
         * (see TODO.md); reject instead of silently misbehaving
         * until that's implemented. */
        sys_exit(-1);
        NOT_REACHED();

    default:
        sys_exit(-1);
        NOT_REACHED();
    }
}

/* Terminates the current process if any byte in the SIZE-byte
 * range starting at UADDR is outside the user address space or
 * unmapped in the current page directory. */
static void
check_user_range(const void *uaddr, size_t size)
{
    struct thread *cur = thread_current();
    const uint8_t *base = uaddr;
    const uint8_t *end = base + size;
    const uint8_t *page;

    if (size == 0) {
        return;
    }

    /* Reject the pointer outright if it (or the end of the range)
     * isn't a user address, including the case where BASE + SIZE
     * overflowed and wrapped around. */
    if (end < base || !is_user_vaddr(base) || !is_user_vaddr(end - 1)) {
        sys_exit(-1);
    }

    for (page = pg_round_down(base); page < end; page += PGSIZE) {
        if (pagedir_get_page(cur->pagedir, page) == NULL) {
            sys_exit(-1);
        }
    }
}

/* Terminates the current process unless USTR is a valid,
 * NUL-terminated string entirely in mapped user memory. */
static void
check_user_string(const char *ustr)
{
    const char *p = ustr;

    check_user_range(p, 1);
    for (;; p++) {
        if (pg_ofs(p) == 0) {
            check_user_range(p, 1);
        }
        if (*p == '\0') {
            break;
        }
    }
}

/* Returns the IDXth word of the syscall's arguments (IDX == 0 is
 * the syscall number itself), validating the pointer first so a
 * malicious or buggy stack pointer can't crash the kernel. */
static uint32_t
arg_word(struct intr_frame *f, int idx)
{
    uint32_t *addr = (uint32_t *)f->esp + idx;

    check_user_range(addr, sizeof *addr);
    return *addr;
}

/* Returns the current thread's open file for FD, or NULL if FD
 * doesn't name a file it has open. */
static struct fd_entry *
lookup_fd(int fd)
{
    struct thread *cur = thread_current();
    struct list_elem *e;

    for (e = list_begin(&cur->fd_list); e != list_end(&cur->fd_list);
         e = list_next(e)) {
        struct fd_entry *fde = list_entry(e, struct fd_entry, elem);

        if (fde->fd == fd) {
            return fde;
        }
    }
    return NULL;
}

/* SYS_EXIT. */
static void
sys_exit(int status)
{
    /* TODO (process.c): once process_wait() tracks per-child exit
     * status, record STATUS there so the parent can retrieve it;
     * for now the parent always sees process_wait()'s -1 stub. */
    printf("%s: exit(%d)\n", thread_name(), status);
    thread_exit();
}

/* SYS_EXEC. */
static tid_t
sys_exec(const char *cmd_line)
{
    check_user_string(cmd_line);

    /* TODO (process.c): process_execute() doesn't yet wait for the
     * child to finish loading or report whether loading succeeded,
     * so a bad command line currently yields a tid whose process
     * exits immediately rather than EXEC failing outright. */
    return process_execute(cmd_line);
}

/* SYS_WAIT. */
static int
sys_wait(tid_t pid)
{
    return process_wait(pid);
}

/* SYS_CREATE. */
static bool
sys_create(const char *file, unsigned initial_size)
{
    bool success;

    check_user_string(file);

    lock_acquire(&filesys_lock);
    success = filesys_create(file, initial_size);
    lock_release(&filesys_lock);

    return success;
}

/* SYS_REMOVE. */
static bool
sys_remove(const char *file)
{
    bool success;

    check_user_string(file);

    lock_acquire(&filesys_lock);
    success = filesys_remove(file);
    lock_release(&filesys_lock);

    return success;
}

/* SYS_OPEN. */
static int
sys_open(const char *file)
{
    struct thread *cur = thread_current();
    struct file *opened;
    struct fd_entry *fde;

    check_user_string(file);

    lock_acquire(&filesys_lock);
    opened = filesys_open(file);
    lock_release(&filesys_lock);

    if (opened == NULL) {
        return -1;
    }

    fde = malloc(sizeof *fde);
    if (fde == NULL) {
        lock_acquire(&filesys_lock);
        file_close(opened);
        lock_release(&filesys_lock);
        return -1;
    }

    fde->fd = cur->fd_next++;
    fde->file = opened;
    list_push_back(&cur->fd_list, &fde->elem);

    return fde->fd;
}

/* SYS_FILESIZE. */
static int
sys_filesize(int fd)
{
    struct fd_entry *fde = lookup_fd(fd);
    int length;

    if (fde == NULL) {
        return -1;
    }

    lock_acquire(&filesys_lock);
    length = file_length(fde->file);
    lock_release(&filesys_lock);

    return length;
}

/* SYS_READ. */
static int
sys_read(int fd, void *buffer, unsigned length)
{
    struct fd_entry *fde;
    int bytes_read;

    check_user_range(buffer, length);

    if (fd == STDIN_FILENO) {
        uint8_t *buf = buffer;
        unsigned i;

        for (i = 0; i < length; i++) {
            buf[i] = input_getc();
        }
        return (int)length;
    }

    if (fd == STDOUT_FILENO) {
        return -1;
    }

    fde = lookup_fd(fd);
    if (fde == NULL) {
        return -1;
    }

    lock_acquire(&filesys_lock);
    bytes_read = file_read(fde->file, buffer, length);
    lock_release(&filesys_lock);

    return bytes_read;
}

/* SYS_WRITE. */
static int
sys_write(int fd, const void *buffer, unsigned length)
{
    struct fd_entry *fde;
    int bytes_written;

    check_user_range(buffer, length);

    if (fd == STDOUT_FILENO) {
        putbuf(buffer, length);
        return (int)length;
    }

    if (fd == STDIN_FILENO) {
        return -1;
    }

    fde = lookup_fd(fd);
    if (fde == NULL) {
        return -1;
    }

    lock_acquire(&filesys_lock);
    bytes_written = file_write(fde->file, buffer, length);
    lock_release(&filesys_lock);

    return bytes_written;
}

/* SYS_SEEK. */
static void
sys_seek(int fd, unsigned position)
{
    struct fd_entry *fde = lookup_fd(fd);

    if (fde == NULL) {
        return;
    }

    lock_acquire(&filesys_lock);
    file_seek(fde->file, position);
    lock_release(&filesys_lock);
}

/* SYS_TELL. */
static unsigned
sys_tell(int fd)
{
    struct fd_entry *fde = lookup_fd(fd);
    off_t pos;

    if (fde == NULL) {
        return 0;
    }

    lock_acquire(&filesys_lock);
    pos = file_tell(fde->file);
    lock_release(&filesys_lock);

    return (unsigned)pos;
}

/* SYS_CLOSE. */
static void
sys_close(int fd)
{
    struct fd_entry *fde = lookup_fd(fd);

    if (fde == NULL) {
        return;
    }

    list_remove(&fde->elem);

    lock_acquire(&filesys_lock);
    file_close(fde->file);
    lock_release(&filesys_lock);

    free(fde);
}

/* Closes every file descriptor the current thread still has open.
 * Called from process_exit() so a dying process can't leak open
 * `struct file's. */
void
syscall_close_all_files(void)
{
    struct thread *cur = thread_current();

    while (!list_empty(&cur->fd_list)) {
        struct fd_entry *fde =
            list_entry(list_pop_front(&cur->fd_list), struct fd_entry, elem);

        lock_acquire(&filesys_lock);
        file_close(fde->file);
        lock_release(&filesys_lock);

        free(fde);
    }
}
