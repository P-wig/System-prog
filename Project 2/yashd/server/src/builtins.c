#include "builtins.h"

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "jobs.h"
#include "session.h"

static void do_fg(session_t *s)
{
    job_t *j = jobs_current(s);

    if (j == NULL)
        return;

    /* Project 1 printed this to stdout; it has to be addressed to the
     * client now, like every other line the shell itself produces. */
    session_printf(s, "%s\n", j->cmdline);

    j->background = 0;

    /* A negative pid signals the whole group, so both halves of a pipeline
     * resume together. No tcsetpgrp: there is no terminal to hand over. */
    if (j->state == JOB_STOPPED)
        kill(-j->pgid, SIGCONT);
    j->state = JOB_RUNNING;

    s->fg_job = j;
    jobs_wait_fg(s, j);
    s->fg_job = NULL;

    if (j->state == JOB_DONE)
        jobs_remove(s, j);
}

static void do_bg(session_t *s)
{
    job_t *j = jobs_recent_stopped(s);

    if (j == NULL)
        return;

    kill(-j->pgid, SIGCONT);
    j->state = JOB_RUNNING;
    j->background = 1;

    jobs_print_one(s, j, '+', "Running");
}

int builtin_try(session_t *s, job_t *job)
{
    const char *name;

    if (job->ncmds != 1 || job->cmds[0].argc == 0)
        return 0;

    name = job->cmds[0].argv[0];

    if (strcmp(name, "jobs") == 0) {
        jobs_print(s);
        return 1;
    }
    if (strcmp(name, "fg") == 0) {
        do_fg(s);
        return 1;
    }
    if (strcmp(name, "bg") == 0) {
        do_bg(s);
        return 1;
    }
    return 0;
}
