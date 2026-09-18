#ifndef PARSE_H
#define PARSE_H

#include "yash.h"

#define PARSE_OK      0
#define PARSE_EMPTY  -1  /* blank line: reprompt, do nothing */
#define PARSE_ERROR  -2  /* malformed line: reprompt, do nothing */

/*
 * Fill *job from one input line (newline already stripped).
 * Tokenizes into job->buf, so job must outlive every use of job->cmds[].argv.
 */
int parse_line(const char *line, job_t *job);

#endif /* PARSE_H */
