#ifndef EXEC_H
#define EXEC_H

#include "yash.h"

/*
 * Forks the job's processes into a new process group, wires the pipe and
 * redirections, then either waits in the foreground or records it as a
 * background job. The only module that creates processes.
 *
 * CRITERIA satisfied here:
 *  - "The left command will have stdout replaced with the input to a pipe"
 *      child 0 does dup2(fds[1], STDOUT_FILENO).
 *  - "The right command will have stdin replaced with the output from the same
 *    pipe" -> child 1 does dup2(fds[0], STDIN_FILENO). Parent and children both
 *    close both raw fds, otherwise the reader never sees EOF.
 *  - "Children within the same pipeline will be started and stopped
 *    simultaneously" -> every child calls setpgid(0, pgid of first child), and
 *    the parent repeats the setpgid to close the fork/exec race. One process
 *    group means the terminal delivers SIGINT/SIGTSTP to both at once.
 *  - "Ctrl-c must quit current foreground process ... and not the shell" and
 *    "Ctrl-z must send SIGTSTP to the current foreground process"
 *      achieved by handing the terminal to the job's pgid for foreground jobs
 *      (sig_give_terminal) and taking it back afterwards.
 *  - "Background a job using &" -> when proto->background, the job is added to
 *    the table and exec_job returns immediately without waiting.
 *  - "Children must inherit the environment from the parent" and "your shell
 *    must search the PATH environment variable" -> execvp(), which uses the
 *    inherited environ and PATH. Never execv() with a hardcoded path.
 *  - "All child processes will be dead on exit" -> children are reparented only
 *    after the shell reaps or kills them; see jobs_reap().
 */
void exec_job(job_t *proto);

#endif /* EXEC_H */
