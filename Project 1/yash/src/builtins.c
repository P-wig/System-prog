#include <string.h>

#include "builtins.h"
#include "jobs.h"
#include "sig.h"

static void do_fg(void)
{
    /* TODO: j = jobs_current(); if none, return.
     *       print j->cmdline; sig_give_terminal(j->pgid);
     *       kill(-j->pgid, SIGCONT) when stopped; j->state = JOB_RUNNING;
     *       j->background = 0; jobs_wait_fg(j); sig_give_terminal(shell_pgid). */
}

static void do_bg(void)
{
    /* TODO: j = most recent STOPPED job; kill(-j->pgid, SIGCONT);
     *       j->state = JOB_RUNNING; j->background = 1; print the job line. */
}

int builtin_try(job_t *job)
{
    const char *name;

    if (job->ncmds != 1 || job->cmds[0].argc == 0)
        return 0;

    name = job->cmds[0].argv[0];

    if (strcmp(name, "jobs") == 0) {
        jobs_print();
        return 1;
    }
    if (strcmp(name, "fg") == 0) {
        do_fg();
        return 1;
    }
    if (strcmp(name, "bg") == 0) {
        do_bg();
        return 1;
    }
    return 0;
}
