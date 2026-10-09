#include "parse.h"

#include <string.h>

/*
 * Carried over from Project 1 unchanged. Single left-to-right pass over
 * space-separated tokens; the parser touches no globals and makes no system
 * calls, so it ports to a threaded server as-is.
 */

/* Splits buf in place on spaces/tabs. Returns token count, -1 on overflow. */
static int tokenize(char *buf, char *tokens[], int max)
{
    /* TODO: strtok_r loop. */
    (void)buf;
    (void)tokens;
    (void)max;
    return -1;
}

static int is_operator(const char *tok)
{
    /* TODO: "<", ">", "2>", "|", "&". */
    (void)tok;
    return 0;
}

/* Returns the cmd_t field tok redirects, or NULL if tok is not a redirection. */
static char **redirect_slot(const char *tok, cmd_t *cmd)
{
    /* TODO: "<" -> &cmd->infile, ">" -> &cmd->outfile, "2>" -> &cmd->errfile. */
    (void)tok;
    (void)cmd;
    return NULL;
}

int parse_line(const char *line, job_t *job)
{
    /*
     * TODO: zero *job, copy line into cmdline and buf, tokenize buf, then
     *       walk the tokens:
     *         redirection -> next token is the target, never an argv entry
     *         "|"         -> starts cmds[1]; a second one is PARSE_ERROR
     *         "&"         -> only legal as the last token, and not with "|"
     *         otherwise   -> append to the current command's argv
     *       Finish by NULL-terminating each argv, setting ncmds, and trimming
     *       the trailing "&" off cmdline so job lines can re-add it uniformly.
     */
    (void)tokenize;
    (void)is_operator;
    (void)redirect_slot;
    (void)line;
    (void)job;
    return PARSE_ERROR;
}
