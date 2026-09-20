#include <stdio.h>
#include <unistd.h>

#include "exec.h"
#include "jobs.h"
#include "redirect.h"
#include "sig.h"

/*
 * C library interfaces used here:
 *   pipe(fds)          - creates a one-way byte channel and fills fds[0] with
 *                        the read end and fds[1] with the write end. The reader
 *                        sees EOF only after every copy of the write end is
 *                        closed, which is why parent and children both close.
 *   fork()             - duplicates the calling process. Returns 0 in the new
 *                        child, the child's pid in the parent, -1 on failure.
 *                        The child inherits open descriptors, the environment,
 *                        and the current signal dispositions.
 *   setpgid(pid, pgid) - puts process `pid` into process group `pgid`; passing
 *                        0 for pgid makes the process a group leader with
 *                        pgid == its own pid. The terminal delivers Ctrl-c and
 *                        Ctrl-z to a whole process group, so this is what makes
 *                        a pipeline start and stop as one unit.
 *   dup2 / close       - see redirect.c; used here to attach the pipe ends.
 *   execvp(file, argv) - replaces the current process image with a new program.
 *                        The `v` means arguments are passed as a NULL-terminated
 *                        vector; the `p` means the PATH environment variable is
 *                        searched when `file` contains no '/'. The environment
 *                        is inherited automatically. It only returns on failure,
 *                        which is how an "invalid command" is detected.
 *   _exit(status)      - terminates immediately without flushing stdio buffers
 *                        or running atexit handlers. Required in a failed child
 *                        so it cannot re-emit output the parent already buffered.
 *   perror(s)          - see redirect.c.
 */

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
     * DONE (job control):
     *  4. job = jobs_add(proto).
     *  5. Background: return without waiting, leaving the job Running.
     *     Foreground: sig_give_terminal(job->pgid); jobs_wait_fg(job);
     *                 sig_give_terminal(shell_pgid);
     */
    int   fds[2] = { -1, -1 };
    pid_t pgid = 0;
    int   i;
    job_t *job;

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

            /* FAQ: an unknown program prints nothing, the shell just reprompts. */
            execvp(cmd->argv[0], cmd->argv);
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

    /* Even a foreground job is tracked, so Ctrl-z can leave it behind. */
    job = jobs_add(proto);
    if (job == NULL) {
        fprintf(stderr, "yash: too many jobs\n");
        return;
    }

    if (job->background)
        return;

    sig_give_terminal(job->pgid);
    jobs_wait_fg(job);
    sig_give_terminal(shell_pgid);

    if (job->state == JOB_DONE)
        jobs_remove(job);
}
