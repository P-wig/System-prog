#include <string.h>

#include "parse.h"

/* Splits job->buf in place on spaces/tabs. Returns token count, -1 on overflow. */
static int tokenize(char *buf, char *tokens[], int max)
{
    int   n = 0;
    char *save = NULL;
    char *tok = strtok_r(buf, " \t", &save);

    while (tok != NULL) {
        if (n == max)
            return -1;
        tokens[n++] = tok;
        tok = strtok_r(NULL, " \t", &save);
    }
    return n;
}

int parse_line(const char *line, job_t *job)
{
    char *tokens[YASH_MAX_TOKENS];
    int   ntok;

    if (line == NULL || strlen(line) > YASH_MAX_LINE)
        return PARSE_ERROR;

    memset(job, 0, sizeof *job);
    strcpy(job->cmdline, line);
    strcpy(job->buf, line);
    job->state = JOB_RUNNING;

    ntok = tokenize(job->buf, tokens, YASH_MAX_TOKENS);
    if (ntok < 0)
        return PARSE_ERROR;
    if (ntok == 0)
        return PARSE_EMPTY;

    /*
     * TODO: walk tokens left to right.
     *   "&"  -> only legal as the last token; sets job->background
     *   "|"  -> starts cmds[1]; a second pipe is a syntax error
     *   "<", ">", "2>" -> next token is the file for the current command;
     *                     missing target or a word after a redirection is an error
     *   otherwise -> append to the current command's argv
     * Finish by NULL-terminating each argv and setting job->ncmds.
     */
    (void)ntok;
    return PARSE_ERROR;
}
