#include "session.h"

#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <unistd.h>

#include "builtins.h"
#include "exec.h"
#include "jobs.h"
#include "log.h"
#include "parse.h"
#include "protocol.h"

session_t *session_create(int cfd, const peer_t *peer)
{
    session_t *s = calloc(1, sizeof *s);

    if (s == NULL)
        return NULL;

    s->cfd  = cfd;
    s->peer = *peer;
    jobs_init(s);
    return s;
}

void session_destroy(session_t *s)
{
    if (s == NULL)
        return;

    jobs_kill_all(s);   /* no child outlives its client */
    close(s->cfd);
    free(s);
}

/* ------------------------------------------------------------------ output */

/*
 * Project 1 wrote to stdout and let the terminal deal with it. Here the
 * daemon's stdout is /dev/null, so everything the shell itself emits has to
 * be addressed to the socket explicitly.
 */
int session_write(session_t *s, const char *buf, size_t n)
{
    size_t off = 0;

    while (off < n) {
        ssize_t w = write(s->cfd, buf + off, n - off);

        if (w < 0) {
            if (errno == EINTR)
                continue;
            s->quit = 1;        /* EPIPE: the client is gone */
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

int session_puts(session_t *s, const char *str)
{
    return session_write(s, str, strlen(str));
}

int session_printf(session_t *s, const char *fmt, ...)
{
    char    buf[YASH_MAX_LINE + 128];
    va_list ap;
    int     n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    if (n <= 0)
        return 0;
    if ((size_t)n >= sizeof buf)
        n = (int)sizeof buf - 1;

    return session_write(s, buf, (size_t)n);
}

int session_prompt(session_t *s)
{
    return session_puts(s, YASH_PROMPT);
}

/* ------------------------------------------------------------------- input */

/*
 * Pops one complete line out of the accumulated buffer. Returns 0 when no
 * '\n' has arrived yet, which is the case fgets() never had to expose.
 */
static int buf_take_line(session_t *s, char *line, size_t n)
{
    char  *nl = memchr(s->inbuf, '\n', s->inlen);
    size_t len;
    size_t consumed;

    if (nl == NULL)
        return 0;

    len      = (size_t)(nl - s->inbuf);
    consumed = len + 1;

    if (len >= n)
        len = n - 1;
    memcpy(line, s->inbuf, len);
    line[len] = '\0';
    if (len > 0 && line[len - 1] == '\r')   /* tolerate CRLF clients */
        line[len - 1] = '\0';

    memmove(s->inbuf, s->inbuf + consumed, s->inlen - consumed);
    s->inlen -= consumed;
    return 1;
}

/* Appends whatever is available. 0 means the peer closed. */
static ssize_t buf_fill(session_t *s)
{
    ssize_t n;

    if (s->inlen == sizeof s->inbuf) {
        s->inlen = 0;       /* a line longer than the buffer: drop it */
        return -1;
    }

    n = read(s->cfd, s->inbuf + s->inlen, sizeof s->inbuf - s->inlen);
    if (n > 0)
        s->inlen += (size_t)n;
    return n;
}

int session_readsocket(session_t *s, char *line, size_t n)
{
    for (;;) {
        ssize_t r;

        if (buf_take_line(s, line, n))
            return 1;

        r = buf_fill(s);
        if (r == 0)
            return 0;
        if (r < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
    }
}

/* ---------------------------------------------------------------- dispatch */

static void handle_ctl(session_t *s, const char *arg)
{
    job_t *j = s->fg_job;

    switch (arg[0]) {
    case CTL_INT:
        /* No tty to route this for us, so the signal is sent by hand.
         * The negative pid covers both halves of a pipeline. */
        if (j != NULL)
            kill(-j->pgid, SIGINT);
        break;

    case CTL_STOP:
        if (j != NULL)
            kill(-j->pgid, SIGTSTP);
        break;

    case CTL_EOF:
        if (j != NULL && j->infd >= 0) {
            close(j->infd);     /* EOF for the command reading our pipe */
            j->infd = -1;
        } else {
            s->quit = 1;        /* at the prompt it ends the session */
        }
        break;

    default:
        break;
    }
}

/* Plain text is stdin for the running command. */
static void handle_text(session_t *s, const char *line)
{
    job_t *j = s->fg_job;

    if (j == NULL || j->infd < 0)
        return;

    if (write(j->infd, line, strlen(line)) < 0 ||
        write(j->infd, "\n", 1) < 0) {   /* readsocket stripped the newline */
        close(j->infd);
        j->infd = -1;
    }
}

static void handle_cmd(session_t *s, char *cmdline)
{
    job_t job;

    if (parse_line(cmdline, &job) != PARSE_OK)
        return;                 /* malformed lines are ignored, as in Project 1 */

    log_command(&s->peer, cmdline);

    if (strcmp(job.cmds[0].argv[0], "quit") == 0 && job.ncmds == 1) {
        s->quit = 1;
        return;
    }

    if (builtin_try(s, &job))
        return;

    exec_job(s, &job);
}

/*
 * The step Project 1 had no equivalent for: a line off the wire is a typed
 * message, not automatically a command.
 */
static void dispatch(session_t *s, char *line)
{
    if (strncmp(line, MSG_CMD, MSG_PREFIX_LEN) == 0)
        handle_cmd(s, line + MSG_PREFIX_LEN);
    else if (strncmp(line, MSG_CTL, MSG_PREFIX_LEN) == 0)
        handle_ctl(s, line + MSG_PREFIX_LEN);
    else
        handle_text(s, line);
}

int session_pump(session_t *s, int timeout_ms)
{
    char           line[YASH_BUF_SIZE];
    fd_set         rfds;
    struct timeval tv;
    int            r;

    /* Several messages can arrive in one packet, so drain the buffer first. */
    while (buf_take_line(s, line, sizeof line))
        dispatch(s, line);

    FD_ZERO(&rfds);
    FD_SET(s->cfd, &rfds);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    r = select(s->cfd + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0)
        return 1;               /* timed out, or EINTR: caller polls the job */

    if (buf_fill(s) == 0)
        return 0;               /* client disconnected */

    while (buf_take_line(s, line, sizeof line))
        dispatch(s, line);

    return 1;
}

/* ------------------------------------------------------------------ thread */

void *session_main(void *arg)
{
    session_t *s = arg;
    char       line[YASH_BUF_SIZE];

    for (;;) {
        jobs_reap(s);                       /* Done lines, before the prompt */

        if (session_prompt(s) < 0)
            break;

        if (session_readsocket(s, line, sizeof line) <= 0)
            break;

        dispatch(s, line);

        if (s->quit)
            break;
    }

    session_destroy(s);
    return NULL;
}
