#include "redirect.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

/* Opens file and moves it onto target, naming the failing path like bash. */
static int open_onto(const char *file, int flags, mode_t mode, int target)
{
    /* TODO: open(); dup2 onto target; close the original. */
    (void)file;
    (void)flags;
    (void)mode;
    (void)target;
    return -1;
}

int redirect_apply(const cmd_t *cmd)
{
    /*
     * TODO:
     *   infile  -> O_RDONLY                        onto STDIN_FILENO
     *   outfile -> O_WRONLY|O_CREAT|O_TRUNC, 0664  onto STDOUT_FILENO
     *   errfile -> same flags                      onto STDERR_FILENO
     * A missing input file already fails here with ENOENT; checking first
     * with stat/access would be a TOCTOU race.
     */
    (void)open_onto;
    (void)cmd;
    return -1;
}
