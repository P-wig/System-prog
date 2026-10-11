#ifndef YASHD_H
#define YASHD_H

#include <sys/types.h>

#include "protocol.h"

/*
 * Daemon-wide paths and the shell data model carried over from Project 1.
 */

#define YASHD_LOG_PATH  "/tmp/yashd.log"
#define YASHD_PID_PATH  "/tmp/yashd.pid"
#define YASHD_BACKLOG   16

#define YASH_MAX_TOKENS 128
#define YASH_MAX_ARGS   64
#define YASH_MAX_CMDS   2    /* at most one pipe per line */
#define YASH_MAX_JOBS   20

/* One executable plus its redirections: the unit that gets exec'd. */
typedef struct {
    char  *argv[YASH_MAX_ARGS + 1]; /* NULL terminated, points into job_t.buf */
    int    argc;
    char  *infile;                  /* <  target, NULL if unused */
    char  *outfile;                 /* >  target, NULL if unused */
    char  *errfile;                 /* 2> target, NULL if unused */
    pid_t  pid;                     /* 0 once reaped */
} cmd_t;

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} job_state_t;

/* Everything typed on one line: 1 or 2 commands sharing a pgid. */
typedef struct job {
    int          jid;
    pid_t        pgid;
    job_state_t  state;
    int          background;
    int          ncmds;
    int          nlive;                      /* children not yet reaped */
    /* Write end of the pipe feeding this job's stdin. Plain-text messages
     * from the client are forwarded here; CTL d closes it. -1 when unused. */
    int          infd;
    cmd_t        cmds[YASH_MAX_CMDS];
    char         cmdline[YASH_MAX_LINE + 1]; /* pristine copy, for `jobs` */
    char         buf[YASH_MAX_LINE + 1];     /* mutable copy, argv points here */
    struct job  *next;
} job_t;

#endif /* YASHD_H */
