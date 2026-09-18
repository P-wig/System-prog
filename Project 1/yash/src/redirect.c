#include "redirect.h"

int redirect_apply(const cmd_t *cmd)
{
    /*
     * TODO:
     *   infile  -> open(O_RDONLY),                     dup2 onto STDIN_FILENO
     *   outfile -> open(O_WRONLY|O_CREAT|O_TRUNC,0644) dup2 onto STDOUT_FILENO
     *   errfile -> same flags,                         dup2 onto STDERR_FILENO
     * Close the original descriptor after each dup2.
     * On failure: perror(file) and return -1 so the child can _exit(1).
     */
    (void)cmd;
    return 0;
}
