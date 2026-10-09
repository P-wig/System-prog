#include "jobs.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/select.h>
#include <sys/wait.h>

#include "session.h"

void jobs_init(session_t *s)
{
    /* TODO: s->job_list = NULL; s->fg_job = NULL. */
    (void)s;
}

/* argv/infile/outfile point into src->buf, so retarget them at dst->buf. */
static void rebase_pointers(job_t *dst, const job_t *src)
{
    /* TODO: dst->buf + (old pointer - src->buf) for every argv entry and file. */
    (void)dst;
    (void)src;
}

job_t *jobs_add(session_t *s, const job_t *proto)
{
    /* TODO: malloc, copy *proto, rebase_pointers, jid = tail->jid + 1, append. */
    (void)rebase_pointers;
    (void)s;
    (void)proto;
    return NULL;
}

void jobs_remove(session_t *s, job_t *job)
{
    /* TODO: unlink from s->job_list and free. */
    (void)s;
    (void)job;
}

job_t *jobs_current(session_t *s)
{
    /* TODO: last job in the list that is RUNNING or STOPPED. */
    (void)s;
    return NULL;
}

job_t *jobs_recent_stopped(session_t *s)
{
    /* TODO: last job in the list that is STOPPED. */
    (void)s;
    return NULL;
}

job_t *jobs_find_pid(session_t *s, pid_t pid)
{
    /* TODO: scan jobs and their cmds[] for a matching pid. */
    (void)s;
    (void)pid;
    return NULL;
}

void jobs_reap(session_t *s)
{
    /* TODO: waitpid(-1, WNOHANG|WUNTRACED|WCONTINUED) until it returns 0,
     *       then emit a Done line to the socket for each finished job. */
    (void)s;
}

void jobs_wait_fg(session_t *s, job_t *job)
{
    /*
     * TODO: there is no tty to deliver SIGINT/SIGTSTP, so this cannot simply
     *       block in waitpid -- the CTL message that interrupts the job
     *       arrives on the socket. Loop on
     *           select(cfd) + waitpid(-job->pgid, WNOHANG | WUNTRACED)
     *       handling any CTL line that arrives, until every child exits or
     *       one of them stops.
     */
    (void)s;
    (void)job;
}

void jobs_print(session_t *s)
{
    /* TODO: walk the list oldest to newest, "+" on the current job. */
    (void)s;
}

void jobs_print_one(session_t *s, const job_t *job, char mark, const char *status)
{
    /* TODO: "[%d] %c %s\t%s%s" into a buffer, then session_write. */
    (void)s;
    (void)job;
    (void)mark;
    (void)status;
}

void jobs_kill_all(session_t *s)
{
    /* TODO: kill(-pgid, SIGKILL) for every job, then reap and free them.
     *       SIGKILL reaches stopped processes without a preceding SIGCONT. */
    (void)s;
}
