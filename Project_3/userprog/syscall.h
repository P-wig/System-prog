#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

/* Closes every file descriptor still open for the current thread.
 * Called by process_exit() so a dying process doesn't leak open
 * `struct file's. */
void syscall_close_all_files(void);

/* Acquires/releases the global lock guarding every filesys_*()/
 * file_*() call (see the comment on the lock's definition in
 * syscall.c). process.c must hold this around its own file
 * system accesses in process_execute()/load()/process_exit(), the
 * same as every syscall handler does, since the filesystem isn't
 * reentrant. */
void syscall_filesys_lock_acquire(void);
void syscall_filesys_lock_release(void);

#endif /* userprog/syscall.h */
