#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#include "redirect.h"

/*
 * C library interfaces used here:
 *   open(path, flags, mode) - returns the lowest unused file descriptor for the
 *                             file, or -1 with errno set.
 *                               O_RDONLY  open for reading; fails (ENOENT) if
 *                                         the file is missing, which is exactly
 *                                         the required `<` behavior.
 *                               O_WRONLY  open for writing.
 *                               O_CREAT   create the file if absent; only then
 *                                         is the mode argument used.
 *                               O_TRUNC   shrink an existing file to 0 bytes.
 *                             The mode is the permission set required by the
 *                             course FAQ: owner and group read/write, others
 *                             read (0664), further masked by the process umask.
 *   dup2(old, new)          - makes descriptor `new` refer to the same open file
 *                             as `old`, silently closing whatever `new` was.
 *                             This is what "replaces stdin/stdout" means: fd 0
 *                             and 1 are just numbers pointing at open files.
 *   close(fd)               - releases a descriptor. The underlying file stays
 *                             open as long as another descriptor refers to it,
 *                             which is why closing the original after dup2 is
 *                             safe and necessary to avoid leaking fds.
 *   perror(s)               - prints "s: " followed by the text for the current
 *                             errno, e.g. "nofile: No such file or directory".
 */

/* Opens file and moves it onto target, naming the failing path like bash does. */
static int open_onto(const char *file, int flags, mode_t mode, int target)
{
    int fd = open(file, flags, mode);

    if (fd < 0) {
        perror(file);
        return -1;
    }
    if (fd != target) {
        if (dup2(fd, target) < 0) {
            perror(file);
            close(fd);
            return -1;
        }
        close(fd);
    }
    return 0;
}

int redirect_apply(const cmd_t *cmd)
{
    /*
     * DONE:
     *   infile  -> open(O_RDONLY),                     dup2 onto STDIN_FILENO
     *   outfile -> open(O_WRONLY|O_CREAT|O_TRUNC,0644) dup2 onto STDOUT_FILENO
     *   errfile -> same flags,                         dup2 onto STDERR_FILENO
     * Close the original descriptor after each dup2.
     * On failure: perror(file) and return -1 so the child can _exit(1).
     */
    const int    out_flags = O_WRONLY | O_CREAT | O_TRUNC;
    const mode_t out_mode  = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;

    if (cmd->infile != NULL &&
        open_onto(cmd->infile, O_RDONLY, 0, STDIN_FILENO) < 0)
        return -1;

    if (cmd->outfile != NULL &&
        open_onto(cmd->outfile, out_flags, out_mode, STDOUT_FILENO) < 0)
        return -1;

    if (cmd->errfile != NULL &&
        open_onto(cmd->errfile, out_flags, out_mode, STDERR_FILENO) < 0)
        return -1;

    return 0;
}
