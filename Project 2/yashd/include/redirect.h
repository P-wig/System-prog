#ifndef REDIRECT_H
#define REDIRECT_H

#include "yashd.h"

/*
 * Opens cmd's redirection targets and dup2's them over the standard fds.
 * Returns 0, or -1 after reporting the offending file.
 *
 * Must be called in the child, after the socket and pipe fds have been
 * dup2'd, so an explicit `> file` still overrides both.
 */
int redirect_apply(const cmd_t *cmd);

#endif /* REDIRECT_H */
