#include <stdio.h>
#include <unistd.h>

#include "exec.h"
#include "jobs.h"
#include "redirect.h"
#include "sig.h"

void exec_job(job_t *proto)
{
    /*
     * DONE:
     *  1. If ncmds == 2, pipe(fds).
     *  2. For each command: fork().
     *       child : setpgid(0, pgid);            (pgid = 0 for the first child)
     *               sig_reset_child();
     *               dup2 pipe ends, close both fds;
     *               redirect_apply(cmd);
     *               execvp(argv[0], argv); perror; _exit(127);
     *       parent: record pid, setpgid(pid, pgid) too (race-free duplication).
     *  3. Parent closes both pipe fds.
     *
     * TODO (job control):
     *  4. job = jobs_add(proto).
     *  5. Background: print "[jid]+ Running <cmdline>" style line and return.
     *     Foreground: sig_give_terminal(job->pgid); jobs_wait_fg(job);
     *                 sig_give_terminal(shell_pgid);
     */
    int   fds[2] = { -1, -1 };
    pid_t pgid = 0;
    int   i;

    if (proto->ncmds == 2 && pipe(fds) < 0) {
        perror("pipe");
        return;
    }

    for (i = 0; i < proto->ncmds; i++) {
        cmd_t *cmd = &proto->cmds[i];
        pid_t  pid = fork();

        if (pid < 0) {
            perror("fork");
            break;
        }

        if (pid == 0) {
            setpgid(0, pgid);
            sig_reset_child();

            if (fds[0] != -1) {
                if (i == 0)
                    dup2(fds[1], STDOUT_FILENO);
                else
                    dup2(fds[0], STDIN_FILENO);
                close(fds[0]);
                close(fds[1]);
            }

            if (redirect_apply(cmd) < 0)
                _exit(1);

            execvp(cmd->argv[0], cmd->argv);
            perror(cmd->argv[0]);
            _exit(127);
        }

        cmd->pid = pid;
        if (pgid == 0)
            pgid = pid;
        setpgid(pid, pgid);  /* repeated in the parent to close the fork race */
        proto->nlive++;
    }

    /* The reader only sees EOF once every copy of the write end is closed. */
    if (fds[0] != -1) {
        close(fds[0]);
        close(fds[1]);
    }

    if (proto->nlive == 0)
        return;

    proto->pgid = pgid;

    sig_give_terminal(pgid);
    jobs_wait_fg(proto);
    sig_give_terminal(shell_pgid);
}
