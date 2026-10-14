#include "parse.h"

#include <string.h>

/*
 * Carried over from Project 1 unchanged. The parser touches no globals and
 * makes no system calls, so it ports to a threaded server as-is.
 */

/* Splits buf in place on spaces/tabs. Returns token count, -1 on overflow. */
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
static char **redirect_slot(const char *tok, cmd_t *cmd)
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
    int    ci = 0;             /* index of the command being filled */
    int    redirect_seen = 0;  /* a redirection ends the arg list */
    cmd_t *cur;

    if (line == NULL || strlen(line) > YASH_MAX_LINE)
        return PARSE_ERROR;

    memset(job, 0, sizeof *job);
    strcpy(job->cmdline, line);
    strcpy(job->buf, line);
    job->state = JOB_RUNNING;
    job->infd  = -1;

    ntok = tokenize(job->buf, tokens, YASH_MAX_TOKENS);
    if (ntok < 0)
        return PARSE_ERROR;
    if (ntok == 0)
        return PARSE_EMPTY;

    cur = &job->cmds[ci];

    for (i = 0; i < ntok; i++) {
        char  *tok = tokens[i];
        char **slot = redirect_slot(tok, cur);

        if (slot != NULL) {
            if (cur->argc == 0)   /* redirection with no command to attach it to */
                return PARSE_ERROR;
            if (*slot != NULL)    /* same direction redirected twice */
                return PARSE_ERROR;
            if (i + 1 >= ntok || is_operator(tokens[i + 1]))
                return PARSE_ERROR;

            *slot = tokens[++i];
            redirect_seen = 1;
            continue;
        }

        if (strcmp(tok, "|") == 0) {
            if (ci + 1 >= YASH_MAX_CMDS)  /* only one | per pipeline */
                return PARSE_ERROR;
            if (cur->argc == 0)           /* nothing on the left of the pipe */
                return PARSE_ERROR;

            cur = &job->cmds[++ci];
            redirect_seen = 0;
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

        /* Redirections follow the command after all its args. */
        if (redirect_seen)
            return PARSE_ERROR;
        if (cur->argc == YASH_MAX_ARGS)
            return PARSE_ERROR;

        cur->argv[cur->argc++] = tok;
    }

    if (cur->argc == 0)
        return PARSE_ERROR;

    /*
     * Keep cmdline free of the trailing '&' so job lines can add it back for
     * any background job, including one moved there later by bg.
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
