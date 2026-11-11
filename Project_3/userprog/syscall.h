#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init(void);

/* Closes every file descriptor still open for the current thread.
 * Called by process_exit() so a dying process doesn't leak open
 * `struct file's. */
void syscall_close_all_files(void);

#endif /* userprog/syscall.h */
