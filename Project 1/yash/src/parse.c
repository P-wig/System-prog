#include <string.h>

#include "parse.h"

/*
 * C library interfaces used here:
 *   memset(p, 0, n)            - fills n bytes with 0; zeroes the whole job_t so
 *                                every pointer field starts as NULL.
 *   strlen(s)                  - number of bytes before the terminating '\0'.
 *   strcpy(dst, src)           - copies src including its '\0'. Safe here only
 *                                because the caller's line is length-checked
 *                                against YASH_MAX_LINE first.
 *   strcmp(a, b)               - 0 when the strings are equal; used for exact
 *                                operator matches like "|" and "&".
 *   strtok_r(s, delims, &save) - splits s in place: overwrites each delimiter
 *                                with '\0' and returns a pointer to the next
 *                                token, or NULL when done. The _r suffix means
 *                                reentrant: the scan position lives in `save`
 *                                instead of a hidden global, so nested or
 *                                concurrent tokenizing cannot corrupt it.
 *                                Because it edits the buffer, the tokens point
 *                                into job->buf and stay valid only as long as
 *                                that buffer does.
 */

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
     * DONE (piping):
     *   "|" -> starts cmds[1]; a second pipe is a syntax error
     * DONE (job control):
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

        if (strcmp(tok, "|") == 0) {
            if (ci + 1 >= YASH_MAX_CMDS)  /* only one | per pipeline */
                return PARSE_ERROR;
            if (cur->argc == 0)           /* nothing on the left of the pipe */
                return PARSE_ERROR;

            cur = &job->cmds[++ci];
            redir_seen = 0;
            continue;
        }

        if (strcmp(tok, "&") == 0) {
            if (i != ntok - 1)  /* & is always the last token on the line */
                return PARSE_ERROR;
            if (ci != 0)        /* | and & are mutually exclusive */
                return PARSE_ERROR;

            job->background = 1;
            continue;
        }

        /* Spec: redirections follow the command after all its args. */
        if (redir_seen)
            return PARSE_ERROR;
        if (cur->argc == YASH_MAX_ARGS)
            return PARSE_ERROR;

        cur->argv[cur->argc++] = tok;
    }

    if (cur->argc == 0)
        return PARSE_ERROR;

    /*
     * Keep cmdline free of the trailing '&' so job lines can add it back for any
     * background job, including one moved there later by bg.
     */
    if (job->background) {
        size_t n = strlen(job->cmdline);

        while (n > 0 && (job->cmdline[n - 1] == '&' ||
                         job->cmdline[n - 1] == ' ' ||
                         job->cmdline[n - 1] == '\t'))
            n--;
        job->cmdline[n] = '\0';
    }

    job->ncmds = ci + 1;
    for (i = 0; i < job->ncmds; i++)
        job->cmds[i].argv[job->cmds[i].argc] = NULL;

    return PARSE_OK;
}
