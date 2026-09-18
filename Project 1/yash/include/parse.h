#ifndef PARSE_H
#define PARSE_H

#include "yash.h"

#define PARSE_OK      0
#define PARSE_EMPTY  -1  /* blank line: reprompt, do nothing */
#define PARSE_ERROR  -2  /* malformed line: reprompt, do nothing */

/*
 * Fill *job from one input line (newline already stripped).
 * Tokenizes into job->buf, so job must outlive every use of job->cmds[].argv.
 *
 * This is the only function that interprets the shell's metacharacters, so it
 * is where every "the next token is ..." rule in the spec is enforced.
 *
 * CRITERIA here:
 *  - "< will replace stdin with the file that is the next token"
 *      the token after `<` is stored in cmds[i].infile, not appended to argv.
 *  - "> will replace stdout with the file that is the next token"
 *      the token after `>` is stored in cmds[i].outfile.
 *  - "A command can have both the redirection symbols (No 2>&1)"
 *      `<` and `>` are handled independently, so `cmd < in > out` sets both.
 *      `2>&1` is not recognized and is a PARSE_ERROR.
 *  - "| separates two commands" / "Only one | must be present"
 *      the first `|` starts cmds[1] and sets ncmds = 2; a second `|` returns
 *      PARSE_ERROR.
 *  - "Background a job using &" / "& will always be the last token"
 *      a trailing `&` sets job->background; `&` anywhere else is PARSE_ERROR.
 *  - "| and & are mutually exclusive"
 *      a line with both ncmds == 2 and background returns PARSE_ERROR.
 *  - "redirections will follow the command after all its args"
 *      lets the scan be a single left-to-right pass with no backtracking.
 *
 * Redirections are only recorded here; they are not opened until the child
 * runs, so a missing input file fails the command and never the shell.
 */
int parse_line(const char *line, job_t *job);

#endif /* PARSE_H */
