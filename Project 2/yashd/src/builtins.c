#include "builtins.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "jobs.h"
#include "session.h"

static void do_fg(session_t *s)
{
    /* TODO: j = jobs_current(s); echo j->cmdline to the socket;
     *       kill(-j->pgid, SIGCONT) when stopped; s->fg_job = j;
     *       jobs_wait_fg(s, j); s->fg_job = NULL. */
    (void)s;
}

static void do_bg(session_t *s)
{
    /* TODO: j = jobs_recent_stopped(s); kill(-j->pgid, SIGCONT);
     *       mark it Running and background; jobs_print_one(s, j, '+', "Running"). */
    (void)s;
}

int builtin_try(session_t *s, job_t *job)
{
    /* TODO: single-command lines named jobs / fg / bg dispatch here and
     *       return 1; everything else returns 0 and goes to exec_job. */
    (void)do_fg;
    (void)do_bg;
    (void)s;
    (void)job;
    return 0;
}
