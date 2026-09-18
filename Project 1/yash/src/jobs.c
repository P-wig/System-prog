#include <stdio.h>

#include "jobs.h"
#include "sig.h"

/* Singly linked list in creation order; head is the oldest job. */
static job_t *job_list;
static int    next_jid = 1;

void jobs_init(void)
{
    job_list = NULL;
    next_jid = 1;
}

job_t *jobs_add(const job_t *proto)
{
    /* TODO: malloc a job_t, copy *proto, fix argv pointers to the new buf,
     *       assign jid = next_jid++, append to job_list. */
    (void)proto;
    return NULL;
}

void jobs_remove(job_t *job)
{
    /* TODO: unlink from job_list and free. Reset next_jid to 1 when empty. */
    (void)job;
}

job_t *jobs_current(void)
{
    /* TODO: last job in the list whose state is RUNNING or STOPPED. */
    return NULL;
}

job_t *jobs_find_pid(pid_t pid)
{
    /* TODO: scan jobs and their cmds[] for a matching pid. */
    (void)pid;
    return NULL;
}

void jobs_reap(void)
{
    /* TODO: loop waitpid(-1, &st, WNOHANG | WUNTRACED | WCONTINUED);
     *       update the owning job's nlive/state; when a background job hits
     *       JOB_DONE print its "Done" line and remove it. */
}

void jobs_wait_fg(job_t *job)
{
    /* TODO: loop waitpid(-job->pgid, &st, WUNTRACED) until every child exited
     *       or one stopped (WIFSTOPPED -> mark JOB_STOPPED, print the job line,
     *       and return so the shell reclaims the terminal). */
    (void)job;
}

void jobs_print_one(const job_t *job, char mark, const char *status)
{
    printf("[%d]%c %s\t%s%s\n", job->jid, mark, status, job->cmdline,
           job->background ? " &" : "");
}

void jobs_print(void)
{
    /* TODO: iterate job_list oldest->newest, mark the current job with '+'
     *       and every other job with '-'. */
}
