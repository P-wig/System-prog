#include "exec.h"

#include <signal.h>
#include <stdio.h>
#include <unistd.h>

#include "jobs.h"
#include "redirect.h"
#include "session.h"

/*
 * exec() resets handlers on its own, but SIG_IGN survives it, and the daemon
 * ignores SIGPIPE and SIGHUP. Without this a command would inherit them
 * ignored and, in SIGPIPE's case, behave wrongly inside a pipeline.
 */
static void reset_child_signals(void)
{
    struct sigaction sa;
    int              i;
    const int        signos[] = { SIGINT, SIGTSTP, SIGQUIT, SIGTTIN,
                                  SIGTTOU, SIGPIPE, SIGHUP, SIGCHLD };

    sa.sa_handler = SIG_DFL;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    for (i = 0; i < (int)(sizeof signos / sizeof signos[0]); i++)
        sigaction(signos[i], &sa, NULL);
}

void exec_job(session_t *s, job_t *proto)
{
    int    fds[2]    = { -1, -1 };   /* the | between two commands */
    int    inpipe[2] = { -1, -1 };   /* client plain text -> command stdin */
    pid_t  pgid = 0;
    job_t *job;
    int    i;

    proto->infd = -1;

    if (pipe(inpipe) < 0)
        return;

    if (proto->ncmds == 2 && pipe(fds) < 0) {
        close(inpipe[0]);
        close(inpipe[1]);
        return;
    }

    for (i = 0; i < proto->ncmds; i++) {
        cmd_t *cmd = &proto->cmds[i];
        pid_t  pid = fork();

        if (pid < 0)
            break;

        if (pid == 0) {
            setpgid(0, pgid);
            reset_child_signals();

            /*
             * The deviation from Project 1 lives in these four lines: the
             * command's standard streams are attached to the connection
             * instead of being inherited from a terminal.
             */
            if (i == 0)
                dup2(inpipe[0], STDIN_FILENO);
            close(inpipe[0]);
            close(inpipe[1]);

            dup2(s->cfd, STDOUT_FILENO);
            dup2(s->cfd, STDERR_FILENO);

            if (fds[0] != -1) {
                if (i == 0)
                    dup2(fds[1], STDOUT_FILENO);   /* the pipe beats the socket */
                else
                    dup2(fds[0], STDIN_FILENO);
                close(fds[0]);
                close(fds[1]);
            }

            if (s->cfd > STDERR_FILENO)
                close(s->cfd);      /* the dups keep the connection alive */

            if (redirect_apply(cmd) < 0)   /* last, so `> file` beats both */
                _exit(1);

            execvp(cmd->argv[0], cmd->argv);
            _exit(127);             /* an unknown command prints nothing */
        }

        cmd->pid = pid;
        if (pgid == 0)
            pgid = pid;
        setpgid(pid, pgid);         /* repeated in the parent to close the race */
        proto->nlive++;
    }

    /* The reader sees EOF only once every copy of the write end is closed. */
    if (fds[0] != -1) {
        close(fds[0]);
        close(fds[1]);
    }
    close(inpipe[0]);

    if (proto->nlive == 0) {
        close(inpipe[1]);
        return;
    }

    proto->pgid = pgid;

    /* A background job must not consume what the user types next, so its
     * stdin is closed immediately and it reads EOF. */
    if (proto->background) {
        close(inpipe[1]);
        proto->infd = -1;
    } else {
        proto->infd = inpipe[1];
    }

    /* Even a foreground job is tracked, so CTL z can leave it behind. */
    job = jobs_add(s, proto);
    if (job == NULL) {
        kill(-pgid, SIGKILL);
        if (proto->infd >= 0)
            close(proto->infd);
        return;
    }

    if (job->background)
        return;

    s->fg_job = job;
    jobs_wait_fg(s, job);
    s->fg_job = NULL;

    if (job->state == JOB_DONE)
        jobs_remove(s, job);
}
