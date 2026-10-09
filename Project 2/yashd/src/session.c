#include "session.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "builtins.h"
#include "exec.h"
#include "jobs.h"
#include "log.h"
#include "parse.h"
#include "protocol.h"

session_t *session_create(int cfd, const peer_t *peer)
{
    /* TODO: calloc a session_t, store cfd and *peer, stdin_pipe = {-1,-1}. */
    (void)cfd;
    (void)peer;
    return NULL;
}

void session_destroy(session_t *s)
{
    /* TODO: jobs_kill_all(s), close cfd and both pipe ends, free(s). */
    (void)s;
}

int session_write(session_t *s, const char *buf, size_t n)
{
    /* TODO: write() in a loop until n bytes are out; -1 on EPIPE. */
    (void)s;
    (void)buf;
    (void)n;
    return -1;
}

int session_puts(session_t *s, const char *str)
{
    return session_write(s, str, strlen(str));
}

int session_prompt(session_t *s)
{
    return session_puts(s, YASH_PROMPT);
}

int session_readsocket(session_t *s, char *line, size_t n)
{
    /*
     * TODO: if s->inbuf already holds a '\n', pop that line out of it.
     *       Otherwise read() more bytes onto the end of inbuf and retry.
     *       A single read may deliver half a line or several lines, so the
     *       remainder must stay buffered for the next call.
     */
    (void)s;
    (void)line;
    (void)n;
    return -1;
}

/* CMD -> run it, CTL -> signal the foreground job, anything else -> stdin. */
static void dispatch(session_t *s, char *line)
{
    /*
     * TODO:
     *   strncmp(line, MSG_CMD, MSG_PREFIX_LEN) == 0
     *       -> log_command(&s->peer, line + MSG_PREFIX_LEN);
     *          parse_line(); builtin_try() || exec_job();
     *   strncmp(line, MSG_CTL, MSG_PREFIX_LEN) == 0
     *       -> CTL_INT  : kill(-s->fg_job->pgid, SIGINT)
     *          CTL_STOP : kill(-s->fg_job->pgid, SIGTSTP)
     *          CTL_EOF  : close(s->stdin_pipe[1]) or set s->quit at the prompt
     *   otherwise
     *       -> write the text to s->stdin_pipe[1]
     */
    (void)s;
    (void)line;
}

void *session_main(void *arg)
{
    /*
     * TODO: the Project 1 read loop with readline replaced by readsocket.
     *   jobs_init(s);
     *   for (;;) {
     *       jobs_reap(s);
     *       session_prompt(s);
     *       if (session_readsocket(s, line, sizeof line) <= 0) break;
     *       dispatch(s, line);
     *       if (s->quit) break;
     *   }
     *   session_destroy(s);
     */
    (void)dispatch;
    (void)arg;
    return NULL;
}
