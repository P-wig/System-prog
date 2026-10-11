#include "redirect.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Unchanged from Project 1. It runs in the child, where stderr has already
 * been pointed at the socket, so perror() reaches the client on its own.
 */

/* Opens file and moves it onto target, naming the failing path like bash. */
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
    const int    out_flags = O_WRONLY | O_CREAT | O_TRUNC;
    const mode_t out_mode  = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH;

    /* O_RDONLY already fails with ENOENT on a missing file; checking first
     * with stat/access would be a TOCTOU race. */
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
