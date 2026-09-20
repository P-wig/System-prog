#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "builtins.h"
#include "jobs.h"
#include "sig.h"

/*
 * C library interfaces used here:
 *   kill(pid, sig)   - sends a signal. A negative pid means "every process in
 *                      group -pid", so kill(-pgid, SIGCONT) resumes both halves
 *                      of a pipeline together. Despite the name it sends any
 *                      signal, not only fatal ones.
 *   SIGCONT          - resumes a stopped process. It takes effect even while the
 *                      target is stopped, unlike most signals.
 *   strcmp / printf  - see parse.c and jobs.c.
 */

static void do_fg(void)
{
    /* DONE: j = jobs_current(); if none, return.
     *       print j->cmdline; sig_give_terminal(j->pgid);
     *       kill(-j->pgid, SIGCONT) when stopped; j->state = JOB_RUNNING;
     *       j->background = 0; jobs_wait_fg(j); sig_give_terminal(shell_pgid). */
    job_t *j = jobs_current();

    if (j == NULL)
        return;

    printf("%s\n", j->cmdline);
    fflush(stdout);

    j->background = 0;
    sig_give_terminal(j->pgid);

    if (j->state == JOB_STOPPED)
        kill(-j->pgid, SIGCONT);
    j->state = JOB_RUNNING;

    jobs_wait_fg(j);
    sig_give_terminal(shell_pgid);

    if (j->state == JOB_DONE)
        jobs_remove(j);
}

static void do_bg(void)
{
    /* DONE: j = most recent STOPPED job; kill(-j->pgid, SIGCONT);
     *       j->state = JOB_RUNNING; j->background = 1; print the job line. */
    job_t *j = jobs_recent_stopped();

    if (j == NULL)
        return;

    kill(-j->pgid, SIGCONT);
    j->state = JOB_RUNNING;
    j->background = 1;

    jobs_print_one(j, '+', "Running");
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
