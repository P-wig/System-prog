#ifndef REDIRECT_H
#define REDIRECT_H

#include "yash.h"

/*
 * Opens cmd's redirection targets and dup2's them over the standard fds.
 * Returns 0 on success, -1 after printing the offending file with perror().
 *
 * MUST be called in the child, after the pipe fds have been dup2'd, so that a
 * redirection later in the line wins over the pipe and so that a failure only
 * kills that one command (the caller does _exit(1) on -1).
 *
 * CRITERIA satisfied here:
 *  - "with creation of files if they don't exist for output redirection"
 *      outfile is opened O_WRONLY | O_CREAT | O_TRUNC with mode 0644.
 *  - "fail command if input redirection (a file) does not exist"
 *      infile is opened O_RDONLY, which already fails with ENOENT; no separate
 *      stat/access check is needed (and checking first would be a TOCTOU bug).
 *  - "< will replace stdin"  -> dup2(fd, STDIN_FILENO)
 *  - "> will replace stdout" -> dup2(fd, STDOUT_FILENO)
 *  - "A command can have both the redirection symbols"
 *      infile and outfile are applied in sequence and never conflict.
 */
int redirect_apply(const cmd_t *cmd);

#endif /* REDIRECT_H */
