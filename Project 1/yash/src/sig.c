#include <signal.h>
#include <unistd.h>

#include "sig.h"

/*
 * C library interfaces used here:
 *   sigaction(signo, &new, &old) - installs a disposition for one signal. It is
 *                                  preferred over signal() because its behavior
 *                                  is precisely specified rather than varying
 *                                  between systems.
 *                                    sa_handler  SIG_DFL (default action),
 *                                                SIG_IGN (discard), or a
 *                                                function pointer.
 *                                    sa_mask     extra signals blocked while the
 *                                                handler runs.
 *                                    sa_flags    0 here; omitting SA_RESTART
 *                                                means an interrupted blocking
 *                                                call fails with EINTR instead
 *                                                of silently resuming, which is
 *                                                what lets fgets return so the
 *                                                shell can reprompt.
 *   sigemptyset(&set)            - initializes a signal set to empty. A
 *                                  sigset_t must always be initialized this way
 *                                  rather than by zeroing it manually.
 *   getpid()                     - the calling process's id.
 *   setpgid(pid, pgid)           - see exec.c; used here so the shell is its own
 *                                  process group leader.
 *   tcsetpgrp(fd, pgid)          - sets the foreground process group of the
 *                                  terminal attached to fd. Only that group may
 *                                  read from the terminal and receives Ctrl-c
 *                                  and Ctrl-z. A process calling this while in a
 *                                  background group is sent SIGTTOU, which is
 *                                  why SIGTTOU is ignored first.
 */

pid_t shell_pgid;
int   shell_terminal = STDIN_FILENO;

/* Absorbs SIGINT/SIGTSTP typed at the prompt; the EINTR is the useful part. */
static void noop_handler(int signo)
{
    (void)signo;
}

static void set_handler(int signo, void (*handler)(int))
{
    struct sigaction sa;

    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  /* no SA_RESTART: fgets must return EINTR so we reprompt */
    sigaction(signo, &sa, NULL);
}

void sig_init_shell(void)
{
    /* DONE: shell_pgid = getpid(); setpgid(shell_pgid, shell_pgid);
     *       tcsetpgrp(shell_terminal, shell_pgid);
     *       neutralize SIGINT, SIGTSTP, SIGTTIN, SIGTTOU, SIGQUIT.
     * SIGCHLD stays SIG_DFL on purpose: jobs_reap() polls with WNOHANG from the
     * read loop, which is where the spec wants Done lines printed. */
    set_handler(SIGINT,  noop_handler);
    set_handler(SIGTSTP, noop_handler);
    set_handler(SIGQUIT, noop_handler);
    set_handler(SIGTTIN, SIG_IGN);
    set_handler(SIGTTOU, SIG_IGN);

    shell_pgid = getpid();
    setpgid(shell_pgid, shell_pgid);
    tcsetpgrp(shell_terminal, shell_pgid);
}

void sig_reset_child(void)
{
    /* DONE: restore SIG_DFL for SIGINT, SIGTSTP, SIGTTIN, SIGTTOU, SIGQUIT. */
    set_handler(SIGINT,  SIG_DFL);
    set_handler(SIGTSTP, SIG_DFL);
    set_handler(SIGQUIT, SIG_DFL);
    set_handler(SIGTTIN, SIG_DFL);
    set_handler(SIGTTOU, SIG_DFL);
}

void sig_give_terminal(pid_t pgid)
{
    /* DONE: tcsetpgrp(shell_terminal, pgid) with SIGTTOU already ignored. */
    if (pgid > 0)
        tcsetpgrp(shell_terminal, pgid);
}
