#ifndef BUILTINS_H
#define BUILTINS_H

#include "session.h"

/*
 * Runs jobs/fg/bg in the session thread itself, since they mutate the
 * session's job table. Returns 1 if the line was a builtin and has been
 * handled, 0 otherwise. Their output goes to the socket like any command's.
 */
int builtin_try(session_t *s, job_t *job);

#endif /* BUILTINS_H */
