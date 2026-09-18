#ifndef BUILTINS_H
#define BUILTINS_H

#include "yash.h"

/*
 * Runs jobs/fg/bg in the shell process itself (they must not be forked).
 * Returns 1 if the job was a builtin and has been handled, 0 otherwise.
 */
int builtin_try(job_t *job);

#endif /* BUILTINS_H */
