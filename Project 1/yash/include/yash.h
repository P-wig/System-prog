#ifndef YASH_H
#define YASH_H

#include <sys/types.h>

/* Spec limits. */
#define YASH_MAX_LINE   200
#define YASH_MAX_TOKENS 128  /* tokens are space separated, so <= (200+1)/2 */
#define YASH_MAX_ARGS   64
#define YASH_MAX_CMDS   2    /* at most one pipe per line */

/* One executable plus its redirections. */
typedef struct {
    char  *argv[YASH_MAX_ARGS + 1]; /* NULL terminated, points into job_t.buf */
    int    argc;
    char  *infile;                  /* <  target, NULL if unused */
    char  *outfile;                 /* >  target, NULL if unused */
    char  *errfile;                 /* 2> target, NULL if unused */
    pid_t  pid;
} cmd_t;

typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} job_state_t;

/* A job is everything typed on one line: 1 or 2 commands sharing a pgid. */
typedef struct job {
    int          jid;                        /* 1-based, shown by `jobs` */
    pid_t        pgid;
    job_state_t  state;
    int          background;                 /* line ended with & */
    int          ncmds;
    int          nlive;                      /* children not yet reaped */
    cmd_t        cmds[YASH_MAX_CMDS];
    char         cmdline[YASH_MAX_LINE + 1]; /* pristine copy, for `jobs` output */
    char         buf[YASH_MAX_LINE + 1];     /* mutable copy, argv points here */
    struct job  *next;
} job_t;

#endif /* YASH_H */
