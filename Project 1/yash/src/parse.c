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

static int is_operator(const char *tok)
{
    return strcmp(tok, "<") == 0 || strcmp(tok, ">") == 0 ||
           strcmp(tok, "2>") == 0 || strcmp(tok, "|") == 0 ||
           strcmp(tok, "&") == 0;
}

/* Returns the cmd_t field tok redirects, or NULL if tok is not a redirection. */
static char **redir_slot(const char *tok, cmd_t *cmd)
{
    if (strcmp(tok, "<") == 0)
        return &cmd->infile;
    if (strcmp(tok, ">") == 0)
        return &cmd->outfile;
    if (strcmp(tok, "2>") == 0)
        return &cmd->errfile;
    return NULL;
}

int parse_line(const char *line, job_t *job)
{
    char  *tokens[YASH_MAX_TOKENS];
    int    ntok;
    int    i;
    int    ci = 0;          /* index of the command being filled */
    int    redir_seen = 0;  /* a redirection ends the arg list of this command */
    cmd_t *cur;

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

    cur = &job->cmds[ci];

    /*
     * DONE (file redirection): walk tokens left to right.
     *   "<", ">", "2>" -> next token is the file for the current command;
     *                     missing target or a word after a redirection is an error
     *   otherwise -> append to the current command's argv
     *   Finish by NULL-terminating each argv and setting job->ncmds.
     *
     * TODO (piping):
     *   "|" -> starts cmds[1]; a second pipe is a syntax error
     * TODO (job control):
     *   "&" -> only legal as the last token; sets job->background
     */
    for (i = 0; i < ntok; i++) {
        char  *tok = tokens[i];
        char **slot = redir_slot(tok, cur);

        if (slot != NULL) {
            if (cur->argc == 0)   /* redirection with no command to attach it to */
                return PARSE_ERROR;
            if (*slot != NULL)    /* same direction redirected twice */
                return PARSE_ERROR;
            if (i + 1 >= ntok || is_operator(tokens[i + 1]))
                return PARSE_ERROR;

            *slot = tokens[++i];
            redir_seen = 1;
            continue;
        }

        if (strcmp(tok, "|") == 0)
            return PARSE_ERROR;   /* TODO (piping) */

        if (strcmp(tok, "&") == 0)
            return PARSE_ERROR;   /* TODO (job control) */

        /* Spec: redirections follow the command after all its args. */
        if (redir_seen)
            return PARSE_ERROR;
        if (cur->argc == YASH_MAX_ARGS)
            return PARSE_ERROR;

        cur->argv[cur->argc++] = tok;
    }

    if (cur->argc == 0)
        return PARSE_ERROR;

    job->ncmds = ci + 1;
    for (i = 0; i < job->ncmds; i++)
        job->cmds[i].argv[job->cmds[i].argc] = NULL;

    return PARSE_OK;
}
