#ifndef JOBS_H
#define JOBS_H

#include "yash.h"

/*
 * The job control table and every waitpid() call in the shell.
 * Centralizing wait status decoding here is what keeps the Running/Stopped/Done
 * transitions consistent between foreground waits and background sweeps.
 */

void   jobs_init(void);

/*
 * Copies *proto into the table and returns the stored job.
 *
 * CRITERIA: "jobs will print ... [<jobnum>]" -> assigns the next job number.
 * CRITERIA: "Max number of jobs running at the same time: 20" -> refuses to add
 * a 21st live job (returns NULL) so the table is bounded.
 */
job_t *jobs_add(const job_t *proto);
void   jobs_remove(job_t *job);

/*
 * The "+" job: most recently created job that is still Running or Stopped.
 *
 * CRITERIA: "a + or - indicating the current job. Which job would be run with
 * an fg, is indicated with a +" -> this is the single definition of "current"
 * used by both jobs_print() and the fg builtin, so they can never disagree.
 */
job_t *jobs_current(void);
job_t *jobs_find_pid(pid_t pid);

/*
 * Non-blocking status sweep: waitpid(WNOHANG | WUNTRACED | WCONTINUED) until it
 * returns 0, updating each owning job's nlive and state.
 *
 * CRITERIA: "Terminated background jobs will be printed after the newline
 * character sent on stdin with a Done" -> main calls this at the top of the
 * read loop, i.e. after the user's newline and before the next prompt, so Done
 * lines appear exactly where bash puts them. Doing this instead of printing
 * from a SIGCHLD handler also avoids async-signal-unsafe printf.
 *
 * CRITERIA: "SIGCHLD" handling and "All child processes will be dead on exit"
 * -> this is what actually reaps zombies.
 */
void   jobs_reap(void);

/*
 * Blocks on waitpid(-job->pgid, WUNTRACED) until every child of the job has
 * exited, or until one of them stops.
 *
 * CRITERIA: "Ctrl-c must quit current foreground process (if one exists) and
 * not the shell" -> the shell is blocked here, not reading input, and the
 * signal goes to the job's pgid; the loop simply sees the children die.
 * CRITERIA: "Ctrl-z must send SIGTSTP to the current foreground process" ->
 * WIFSTOPPED marks the job JOB_STOPPED and returns so the shell reprompts.
 * CRITERIA: "should not print the process (unlike bash)" -> no output on the
 * SIGINT path.
 * CRITERIA: "fg ... and wait for completion" -> the fg builtin reuses this.
 */
void   jobs_wait_fg(job_t *job);

/*
 * CRITERIA: "jobs will print the job control table similar to bash" with
 * [<jobnum>], + or -, "Stopped"/"Running"/"Done", then the original command:
 *     [1] - Running   sleep 5 &
 *     [2] + Stopped   sleep 5
 * jobs_print() walks the table oldest to newest; jobs_print_one() formats a
 * single row and is shared with bg, exec_job and the Done messages so all four
 * emit an identical format.
 */
void   jobs_print(void);                 /* builtin: jobs */
void   jobs_print_one(const job_t *job, char mark, const char *status);

#endif /* JOBS_H */
