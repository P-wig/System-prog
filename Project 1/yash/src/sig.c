#include <signal.h>
#include <unistd.h>

#include "sig.h"

pid_t shell_pgid;
int   shell_terminal = STDIN_FILENO;

void sig_init_shell(void)
{
    /* TODO: ignore SIGINT, SIGTSTP, SIGTTIN, SIGTTOU, SIGQUIT;
     *       shell_pgid = getpid(); setpgid(shell_pgid, shell_pgid);
     *       tcsetpgrp(shell_terminal, shell_pgid). */
}

void sig_reset_child(void)
{
    /* TODO: restore SIG_DFL for SIGINT, SIGTSTP, SIGTTIN, SIGTTOU, SIGQUIT. */
}

void sig_give_terminal(pid_t pgid)
{
    /* TODO: tcsetpgrp(shell_terminal, pgid) with SIGTTOU already ignored. */
    (void)pgid;
}
