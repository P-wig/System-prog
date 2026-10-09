#include "exec.h"

#include <stdio.h>
#include <unistd.h>

#include "jobs.h"
#include "redirect.h"
#include "session.h"

void exec_job(session_t *s, job_t *proto)
{
    /*
     * TODO:
     *  1. pipe(fds) when ncmds == 2; pipe(s->stdin_pipe) for the child's stdin.
     *  2. For each command: fork().
     *       child : setpgid(0, pgid);
     *               restore SIG_DFL for SIGINT/SIGTSTP/SIGQUIT/SIGTTIN/SIGTTOU;
     *               dup2(s->stdin_pipe[0], STDIN_FILENO);
     *               dup2(s->cfd, STDOUT_FILENO);
     *               dup2(s->cfd, STDERR_FILENO);
     *               dup2 the pipe end, close both fds;
     *               redirect_apply(cmd);        -- last, so `> file` wins
     *               execvp(argv[0], argv); _exit(127);
     *       parent: record pid, setpgid(pid, pgid) too (closes the fork race).
     *  3. Parent closes both pipe fds -- the reader sees EOF only once every
     *     copy of the write end is gone.
     *  4. job = jobs_add(s, proto).
     *     Background: return immediately.
     *     Foreground: s->fg_job = job; jobs_wait_fg(s, job); s->fg_job = NULL.
     *
     * No tcsetpgrp anywhere: a daemon has no controlling terminal. The process
     * group still matters so kill(-pgid, ...) hits both halves of a pipeline.
     */
    (void)s;
    (void)proto;
}
