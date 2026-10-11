#include "jobs.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "session.h"

/* How long jobs_wait_fg sleeps in select() between polls of the job. */
#define FG_POLL_MS 50

void jobs_init(session_t *s)
{
    s->job_list = NULL;
    s->fg_job   = NULL;
}

static int jobs_count(const session_t *s)
{
    const job_t *j;
    int          n = 0;

    for (j = s->job_list; j != NULL; j = j->next)
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

job_t *jobs_add(session_t *s, const job_t *proto)
{
    job_t *job;

    if (jobs_count(s) >= YASH_MAX_JOBS)
        return NULL;

    job = malloc(sizeof *job);
    if (job == NULL)
        return NULL;

    *job = *proto;
    job->next = NULL;
    rebase_pointers(job, proto);

    if (s->job_list == NULL) {
        job->jid = 1;
        s->job_list = job;
    } else {
        job_t *tail = s->job_list;

        while (tail->next != NULL)
            tail = tail->next;
        job->jid = tail->jid + 1;   /* the list is ordered, so the tail is the max */
        tail->next = job;
    }

    return job;
}

void jobs_remove(session_t *s, job_t *job)
{
    job_t **link = &s->job_list;

    while (*link != NULL) {
        if (*link == job) {
            *link = job->next;
            if (job->infd >= 0)
                close(job->infd);
            if (s->fg_job == job)
                s->fg_job = NULL;
            free(job);
            return;
        }
        link = &(*link)->next;
    }
}

job_t *jobs_current(session_t *s)
{
    job_t *j, *found = NULL;

    for (j = s->job_list; j != NULL; j = j->next)
        if (j->state == JOB_RUNNING || j->state == JOB_STOPPED)
            found = j;

    return found;
}

job_t *jobs_recent_stopped(session_t *s)
{
    job_t *j, *found = NULL;

    for (j = s->job_list; j != NULL; j = j->next)
        if (j->state == JOB_STOPPED)
            found = j;

    return found;
}

job_t *jobs_find_pid(session_t *s, pid_t pid)
{
    job_t *j;
    int    i;

    for (j = s->job_list; j != NULL; j = j->next)
        for (i = 0; i < j->ncmds; i++)
            if (j->cmds[i].pid == pid)
                return j;

    return NULL;
}

static void clear_pid(job_t *job, pid_t pid)
{
    int i;

    for (i = 0; i < job->ncmds; i++)
        if (job->cmds[i].pid == pid)
            job->cmds[i].pid = 0;
}

/*
 * Collects status changes for one job. Returns 1 if the job stopped.
 *
 * Project 1 could wait on -1 ("any child") because the shell was the only
 * thing running. Here every session thread shares one process's children, so
 * waiting on -1 would let one client reap another client's job and silently
 * lose the status. Waiting on the job's own pgid keeps each thread to its own.
 */
static int poll_job(job_t *job, int block)
{
    const int flags = WUNTRACED | WCONTINUED | (block ? 0 : WNOHANG);

    while (job->nlive > 0) {
        int   st;
        pid_t r = waitpid(-job->pgid, &st, flags);

        if (r == 0)
            return 0;                       /* nothing ready (WNOHANG) */
        if (r < 0) {
            if (errno == EINTR)
                continue;
            job->nlive = 0;                 /* ECHILD: nothing left to wait for */
            break;
        }

        if (WIFSTOPPED(st)) {
            /* One member stopping means the whole group stopped: same pgid. */
            job->state = JOB_STOPPED;
            return 1;
        }
        if (WIFCONTINUED(st)) {
            job->state = JOB_RUNNING;
            continue;
        }

        clear_pid(job, r);
        job->nlive--;
    }
    return 0;
}

void jobs_reap(session_t *s)
{
    job_t *j, *next;

    for (j = s->job_list; j != NULL; j = j->next)
        if (j->state != JOB_DONE && j->nlive > 0 && poll_job(j, 0) == 0)
            if (j->nlive == 0)
                j->state = JOB_DONE;

    for (j = s->job_list; j != NULL; j = next) {
        next = j->next;
        if (j->state == JOB_DONE) {
            /* A finished job is never "current", so it only earns + when it
             * is the last one left. */
            jobs_print_one(s, j, jobs_current(s) == NULL ? '+' : '-', "Done");
            jobs_remove(s, j);
        }
    }
}

/*
 * The foreground wait, and the one place where losing the terminal really
 * changes the algorithm.
 *
 * Project 1 blocked in waitpid() and let the tty deliver SIGINT/SIGTSTP to
 * the job. Here the interrupt arrives as a CTL message on the socket, so
 * blocking in waitpid() would make the job uninterruptible. Instead the
 * thread alternates between servicing the socket and polling the job.
 */
void jobs_wait_fg(session_t *s, job_t *job)
{
    while (job->nlive > 0) {
        if (!s->quit) {
            if (session_pump(s, FG_POLL_MS) == 0) {
                /* The client vanished; nothing can interrupt the job now. */
                s->quit = 1;
                kill(-job->pgid, SIGKILL);
            }
            if (poll_job(job, 0))
                return;                     /* stopped, not finished */
        } else if (poll_job(job, 1)) {
            return;
        }
    }

    job->state = JOB_DONE;
}

void jobs_print_one(session_t *s, const job_t *job, char mark, const char *status)
{
    /* The trailing & is reattached here, so bg'd jobs show it too. */
    session_printf(s, "[%d] %c %s\t%s%s\n", job->jid, mark, status,
                   job->cmdline, job->background ? " &" : "");
}

void jobs_print(session_t *s)
{
    const job_t *cur = jobs_current(s);
    const job_t *j;

    for (j = s->job_list; j != NULL; j = j->next)
        jobs_print_one(s, j, j == cur ? '+' : '-',
                       j->state == JOB_STOPPED ? "Stopped" : "Running");
}

void jobs_kill_all(session_t *s)
{
    job_t *j, *next;

    for (j = s->job_list; j != NULL; j = next) {
        next = j->next;

        /* SIGKILL cannot be caught and reaches stopped processes without a
         * preceding SIGCONT, so no child survives its client. */
        kill(-j->pgid, SIGKILL);
        if (j->infd >= 0)
            close(j->infd);

        while (j->nlive > 0) {
            int   st;
            pid_t r = waitpid(-j->pgid, &st, 0);

            if (r < 0) {
                if (errno == EINTR)
                    continue;
                break;      /* ECHILD: already gone */
            }
            if (WIFSTOPPED(st) || WIFCONTINUED(st))
                continue;
            j->nlive--;
        }
        free(j);
    }

    s->job_list = NULL;
    s->fg_job   = NULL;
}
