#ifndef REDIRECT_H
#define REDIRECT_H

#include "yash.h"

/* Applies <, >, 2> for one command. Call in the child, after dup2 of pipe fds. */
int redirect_apply(const cmd_t *cmd);

#endif /* REDIRECT_H */
