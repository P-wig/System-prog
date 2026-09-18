#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>

#include "redirect.h"

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
    const int out_flags = O_WRONLY | O_CREAT | O_TRUNC;

    if (cmd->infile != NULL &&
        open_onto(cmd->infile, O_RDONLY, 0, STDIN_FILENO) < 0)
        return -1;

    if (cmd->outfile != NULL &&
        open_onto(cmd->outfile, out_flags, 0644, STDOUT_FILENO) < 0)
        return -1;

    if (cmd->errfile != NULL &&
        open_onto(cmd->errfile, out_flags, 0644, STDERR_FILENO) < 0)
        return -1;

    return 0;
}
