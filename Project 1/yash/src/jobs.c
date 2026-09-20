#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>

#include "jobs.h"
#include "sig.h"

/*
 * C library interfaces used here:
 *   waitpid(pid, &st, flags) - reports a state change of a child and reaps it if
 *                              it died. Returns the pid it reported on, 0 when
 *                              nothing is ready (only possible with WNOHANG),
 *                              or -1 on error.
 *                                pid  -1 = any child, -N = any child in process
 *                                     group N.
 *                                WNOHANG     return 0 immediately instead of
 *                                            blocking; turns this into a poll.
 *                                WUNTRACED   also report children that stopped
 *                                            (Ctrl-z), not just ones that died.
 *                                WCONTINUED  also report children resumed by
 *                                            SIGCONT.
 *   WIFSTOPPED(st)           - macro: the child stopped rather than exited.
 *   WIFCONTINUED(st)         - macro: the child was resumed by SIGCONT.
 *   errno / EINTR            - a blocking call aborted because a signal arrived;
 *                              it is not a real failure, so the loop retries.
 *   malloc(n) / free(p)      - heap allocation. A job outlives the stack frame
 *                              that parsed it, so it must be copied to the heap.
 *   kill(pid, sig)           - see builtins.c; a negative pid signals a whole
 *                              process group.
 *   printf / fflush          - formatted output; flushed so job lines appear
 *                              before the next prompt is written.
 */

/* Singly linked list in creation order; head is the oldest job. */
static job_t *job_list;

void jobs_init(void)
{
    job_list = NULL;
}

static int jobs_count(void)
{
    const job_t *j;
    int          n = 0;

    for (j = job_list; j != NULL; j = j->next)
        n++;
    return n;
}

/* argv/infile/outfile point into src->buf, so retarget them at dst->buf. */
static void rebase_pointers(job_t *dst, const job_t *src)
{
    int i, j;

    for (i = 0; i < dst->ncmds; i++) {
        cmd_t       *d = &dst->cmds[i];
        const cmd_t *s = &src->cmds[i];

        for (j = 0; j < s->argc; j++)
            d->argv[j] = dst->buf + (s->argv[j] - src->buf);
        d->argv[s->argc] = NULL;

        d->infile  = s->infile  ? dst->buf + (s->infile  - src->buf) : NULL;
        d->outfile = s->outfile ? dst->buf + (s->outfile - src->buf) : NULL;
        d->errfile = s->errfile ? dst->buf + (s->errfile - src->buf) : NULL;
    }
}

job_t *jobs_add(const job_t *proto)
{
    /* DONE: malloc a job_t, copy *proto, fix argv pointers to the new buf,
     *       assign the next jid, append to job_list. */
    job_t *job;

    if (jobs_count() >= YASH_MAX_JOBS)
        return NULL;

    job = malloc(sizeof *job);
    if (job == NULL)
        return NULL;

    *job = *proto;
    job->next = NULL;
    rebase_pointers(job, proto);

    if (job_list == NULL) {
        job->jid = 1;
        job_list = job;
    } else {
        job_t *tail = job_list;

        while (tail->next != NULL)
            tail = tail->next;
        job->jid = tail->jid + 1;   /* list is ordered, so the tail holds the max */
        tail->next = job;
    }

    return job;
}

void jobs_remove(job_t *job)
{
    /* DONE: unlink from job_list and free. Numbering restarts at 1 once the
     *       list is empty, which falls out of jobs_add. */
    job_t **link = &job_list;

    while (*link != NULL) {
        if (*link == job) {
            *link = job->next;
            free(job);
            return;
        }
        link = &(*link)->next;
    }
}

job_t *jobs_current(void)
{
    /* DONE: last job in the list whose state is RUNNING or STOPPED. */
    job_t *j, *found = NULL;

    for (j = job_list; j != NULL; j = j->next)
        if (j->state == JOB_RUNNING || j->state == JOB_STOPPED)
            found = j;

    return found;
}

job_t *jobs_recent_stopped(void)
{
    job_t *j, *found = NULL;

    for (j = job_list; j != NULL; j = j->next)
        if (j->state == JOB_STOPPED)
            found = j;

    return found;
}

job_t *jobs_find_pid(pid_t pid)
{
    /* DONE: scan jobs and their cmds[] for a matching pid. */
    job_t *j;
    int    i;

    for (j = job_list; j != NULL; j = j->next)
        for (i = 0; i < j->ncmds; i++)
            if (j->cmds[i].pid == pid)
                return j;

    return NULL;
}

void jobs_reap(void)
{
    /* DONE: loop waitpid(-1, &st, WNOHANG | WUNTRACED | WCONTINUED);
     *       update the owning job's nlive/state; when a background job hits
     *       JOB_DONE print its "Done" line and remove it. */
    job_t *j, *next;
    pid_t  pid;
    int    st;

    while ((pid = waitpid(-1, &st, WNOHANG | WUNTRACED | WCONTINUED)) > 0) {
        j = jobs_find_pid(pid);
        if (j == NULL)
            continue;

        if (WIFSTOPPED(st))
            j->state = JOB_STOPPED;
        else if (WIFCONTINUED(st))
            j->state = JOB_RUNNING;
        else if (--j->nlive == 0)
            j->state = JOB_DONE;
    }

    for (j = job_list; j != NULL; j = next) {
        next = j->next;
        if (j->state == JOB_DONE) {
            /* A finished job is never "current", so it only earns + when it is
             * the last one left. */
            jobs_print_one(j, jobs_current() == NULL ? '+' : '-', "Done");
            jobs_remove(j);
        }
    }
}

void jobs_wait_fg(job_t *job)
{
    /* DONE: loop waitpid(-job->pgid, &st, WUNTRACED) until every child exited
     *       or one stopped (WIFSTOPPED -> mark JOB_STOPPED and return so the
     *       shell reclaims the terminal). Nothing is printed on stop: the spec
     *       wants Ctrl-z silent, unlike bash. */
    int st;

    while (job->nlive > 0) {
        pid_t pid = waitpid(-job->pgid, &st, WUNTRACED);

        if (pid < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        /* One member stopping means the whole group stopped: same pgid. */
        if (WIFSTOPPED(st)) {
            job->state = JOB_STOPPED;
            return;
        }
        job->nlive--;
    }

    job->state = JOB_DONE;
}

void jobs_print_one(const job_t *job, char mark, const char *status)
{
    /* The trailing & is reattached here, so bg'd jobs show it too. */
    printf("[%d] %c %s\t%s%s\n", job->jid, mark, status, job->cmdline,
           job->background ? " &" : "");
    fflush(stdout);
}

void jobs_print(void)
{
    /* DONE: iterate job_list oldest->newest, mark the current job with '+'
     *       and every other job with '-'. */
    const job_t *cur = jobs_current();
    const job_t *j;

    for (j = job_list; j != NULL; j = j->next)
        jobs_print_one(j, j == cur ? '+' : '-',
                       j->state == JOB_STOPPED ? "Stopped" : "Running");
}

void jobs_kill_all(void)
{
    job_t *j, *next;

    for (j = job_list; j != NULL; j = next) {
        next = j->next;
        kill(-j->pgid, SIGKILL);
        free(j);
    }

    job_list = NULL;
}
