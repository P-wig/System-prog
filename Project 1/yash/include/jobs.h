#ifndef JOBS_H
#define JOBS_H

#include "yash.h"

void   jobs_init(void);

/* Copies *proto into the table and returns the stored job. */
job_t *jobs_add(const job_t *proto);
void   jobs_remove(job_t *job);

/* Most recently created job that is still running or stopped ("+" job). */
job_t *jobs_current(void);
job_t *jobs_find_pid(pid_t pid);

/* Non-blocking status sweep; prints "Done" lines for finished bg jobs. */
void   jobs_reap(void);

/* Blocks until the foreground job stops or exits; restores terminal control. */
void   jobs_wait_fg(job_t *job);

void   jobs_print(void);                 /* builtin: jobs */
void   jobs_print_one(const job_t *job, char mark, const char *status);

#endif /* JOBS_H */
