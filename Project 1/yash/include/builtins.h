#ifndef BUILTINS_H
#define BUILTINS_H

#include "yash.h"

/*
 * Runs jobs/fg/bg in the shell process itself (they must not be forked, since
 * they mutate the shell's job table and terminal ownership).
 * Returns 1 if the job was a builtin and has been handled, 0 otherwise.
 * main() calls this before exec_job(), so a builtin never reaches execvp().
 *
 * CRITERIA satisfied by the three commands dispatched here:
 *  - "fg must send SIGCONT to the most recent background or stopped process,
 *    print the process name to stdout, and wait for completion"
 *      -> jobs_current(), print cmdline, kill(-pgid, SIGCONT),
 *         sig_give_terminal(pgid), jobs_wait_fg().
 *  - "bg must send SIGCONT to the most recent stopped process, print the
 *    process name to stdout in the jobs format, and not wait for completion
 *    (as if &)"
 *      -> most recent JOB_STOPPED job, kill(-pgid, SIGCONT), mark it Running
 *         and background, print via jobs_print_one(), then return immediately.
 *  - "jobs will print the job control table similar to bash"
 *      -> delegates to jobs_print().
 */
int builtin_try(job_t *job);

#endif /* BUILTINS_H */
