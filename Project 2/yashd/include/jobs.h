#ifndef JOBS_H
#define JOBS_H

#include "session.h"

/*
 * The session's job table and every waitpid() call in the daemon.
 * Identical to Project 1 except that the list hangs off session_t instead of
 * a file-scope static, so two clients never see each other's jobs.
 */

void   jobs_init(session_t *s);

job_t *jobs_add(session_t *s, const job_t *proto);
void   jobs_remove(session_t *s, job_t *job);

job_t *jobs_current(session_t *s);          /* the "+" job: newest live job */
job_t *jobs_recent_stopped(session_t *s);   /* bg's candidate */
job_t *jobs_find_pid(session_t *s, pid_t pid);

/* Non-blocking sweep: updates states and reports Done jobs to the client. */
void   jobs_reap(session_t *s);

/*
 * Waits for the foreground job while staying responsive to CTL messages:
 * select() on the client socket with waitpid(WNOHANG), since there is no tty
 * to deliver SIGINT/SIGTSTP for us.
 */
void   jobs_wait_fg(session_t *s, job_t *job);

void   jobs_print(session_t *s);            /* builtin: jobs */
void   jobs_print_one(session_t *s, const job_t *job, char mark, const char *status);

/* SIGKILL every group on session teardown; no child outlives its client. */
void   jobs_kill_all(session_t *s);

#endif /* JOBS_H */
