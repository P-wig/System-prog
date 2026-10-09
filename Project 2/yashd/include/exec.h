#ifndef EXEC_H
#define EXEC_H

#include "session.h"

/*
 * Forks the job's processes into a new process group, wires the pipe and
 * redirections, then either waits in the foreground or records it as a
 * background job. The only module that creates processes.
 *
 * Differs from Project 1 in the child's fd setup: stdout and stderr are
 * dup2'd onto the client socket and stdin onto the session's stdin pipe,
 * before redirect_apply() gets the last word.
 */
void exec_job(session_t *s, job_t *proto);

#endif /* EXEC_H */
