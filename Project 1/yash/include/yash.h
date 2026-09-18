#ifndef YASH_H
#define YASH_H

#include <sys/types.h>

/*
 * Shared data model. Every module reads and writes these two structs, so the
 * spec's limits are encoded here once.
 *
 * CRITERIA -> CONSTANT
 *   "Lines will not exceed 200 characters"            -> YASH_MAX_LINE
 *   "Only one | must be present in each pipeline"     -> YASH_MAX_CMDS == 2
 *   "Max number of jobs running at the same time: 20" -> YASH_MAX_JOBS
 */
#define YASH_MAX_LINE   200
#define YASH_MAX_TOKENS 128  /* tokens are space separated, so <= (200+1)/2 */
#define YASH_MAX_ARGS   64
#define YASH_MAX_CMDS   2    /* at most one pipe per line */
#define YASH_MAX_JOBS   20

/*
 * One executable plus its redirections: the unit that gets exec'd.
 *
 * CRITERIA:
 *  - "< will replace stdin with the file that is the next token"  -> infile
 *  - "> will replace stdout with the file that is the next token" -> outfile
 *  - "A command can have both the redirection symbols" -> infile and outfile are
 *    independent fields, so both may be set on the same command.
 *  - "(No 2>&1)" -> stderr duplication is out of scope; errfile exists only for a
 *    literal `2> file` and stays NULL otherwise.
 */
typedef struct {
    char  *argv[YASH_MAX_ARGS + 1]; /* NULL terminated, points into job_t.buf */
    int    argc;
    char  *infile;                  /* <  target, NULL if unused */
    char  *outfile;                 /* >  target, NULL if unused */
    char  *errfile;                 /* 2> target, NULL if unused */
    pid_t  pid;
} cmd_t;

/* CRITERIA: the three strings `jobs` must be able to print for a job. */
typedef enum {
    JOB_RUNNING,
    JOB_STOPPED,
    JOB_DONE
} job_state_t;

/*
 * A job is everything typed on one line: 1 or 2 commands sharing a pgid.
 *
 * CRITERIA:
 *  - "Children within the same pipeline will be started and stopped
 *    simultaneously" -> both commands share one `pgid`, so a single
 *    kill(-pgid, sig) hits the whole pipeline at once.
 *  - "jobs will print ... [<jobnum>]" -> jid
 *  - "a 'Stopped' or 'Running' indicating the status" -> state
 *  - "and finally the original command" -> cmdline, kept verbatim because
 *    parsing overwrites the separators in buf with NULs.
 *  - "Background a job using &" -> background
 */
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
