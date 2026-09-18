#ifndef EXEC_H
#define EXEC_H

#include "yash.h"

/*
 * Forks the job's processes into a new process group, wires the pipe and
 * redirections, then either waits in the foreground or records it as a
 * background job.
 */
void exec_job(job_t *proto);

#endif /* EXEC_H */
