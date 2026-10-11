#ifndef SESSION_H
#define SESSION_H

#include <stddef.h>

#include "transport.h"
#include "yashd.h"

/*
 * All per-client state. Project 1 kept the job table in a file-scope static;
 * with one thread per client that would be shared mutable state, so it lives
 * here instead and is threaded through jobs.c, exec.c and builtins.c.
 */
typedef struct {
    int     cfd;                    /* the client socket */
    peer_t  peer;

    job_t  *job_list;               /* was the static in jobs.c */
    job_t  *fg_job;                 /* target of CTL c / CTL z, or NULL */

    char    inbuf[YASH_BUF_SIZE];   /* partial line held across reads */
    size_t  inlen;

    int     quit;                   /* CTL d at the prompt, or peer closed */
} session_t;

session_t *session_create(int cfd, const peer_t *peer);
void       session_destroy(session_t *s);

/* pthread entry point; owns the session for its whole lifetime. */
void *session_main(void *arg);

/*
 * One complete '\n'-terminated line from the socket, newline stripped.
 * A stream socket has no message boundaries, so this accumulates into
 * s->inbuf until a full line is available.
 * Returns 1 on a line, 0 when the peer closed, -1 on error.
 */
int session_readsocket(session_t *s, char *line, size_t n);

/*
 * Services the socket while a foreground job runs: handles CTL messages and
 * forwards plain text to the job's stdin. Waits at most timeout_ms.
 * Returns 1 normally, 0 when the client closed the connection.
 */
int session_pump(session_t *s, int timeout_ms);

/*
 * Everything the shell itself prints. Project 1 used printf; the daemon's
 * stdout is /dev/null, so these go to the socket instead.
 */
int session_write(session_t *s, const char *buf, size_t n);
int session_puts(session_t *s, const char *str);
int session_printf(session_t *s, const char *fmt, ...);
int session_prompt(session_t *s);

#endif /* SESSION_H */
