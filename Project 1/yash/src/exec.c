#include "exec.h"
#include "jobs.h"
#include "redirect.h"
#include "sig.h"

void exec_job(job_t *proto)
{
    /*
     * TODO:
     *  1. If ncmds == 2, pipe(fds).
     *  2. For each command: fork().
     *       child : setpgid(0, pgid);            (pgid = 0 for the first child)
     *               sig_reset_child();
     *               dup2 pipe ends, close both fds;
     *               redirect_apply(cmd);
     *               execvp(argv[0], argv); perror; _exit(127);
     *       parent: record pid, setpgid(pid, pgid) too (race-free duplication).
     *  3. Parent closes both pipe fds.
     *  4. job = jobs_add(proto).
     *  5. Background: print "[jid]+ Running <cmdline>" style line and return.
     *     Foreground: sig_give_terminal(job->pgid); jobs_wait_fg(job);
     *                 sig_give_terminal(shell_pgid);
     */
    (void)proto;
}
