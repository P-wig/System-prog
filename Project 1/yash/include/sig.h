#ifndef SIG_H
#define SIG_H

#include <sys/types.h>

/*
 * Signal dispositions and terminal ownership.
 *
 * The whole Ctrl-c / Ctrl-z section of the spec reduces to one rule: the
 * terminal delivers SIGINT/SIGTSTP to the *foreground process group*. So the
 * shell never needs handlers for them; it only needs to (a) ignore them itself
 * and (b) make sure the foreground job, not the shell, owns the terminal.
 */

extern pid_t shell_pgid;
extern int   shell_terminal;   /* STDIN_FILENO */

/*
 * Puts the shell in its own process group, takes the terminal, and sets the
 * job-control signals to SIG_IGN.
 *
 * CRITERIA:
 *  - "Ctrl-c ... [must] not [quit] the shell"   -> SIGINT ignored.
 *  - "The shell will not be stopped on SIGTSTP" -> SIGTSTP ignored.
 *  - "should not print the process (unlike bash)" -> because these are ignored
 *    rather than handled, there is no handler that could print anything.
 *  - SIGTTIN/SIGTTOU ignored so the shell's own tcsetpgrp() cannot stop it.
 */
void sig_init_shell(void);

/*
 * Restores SIG_DFL for SIGINT, SIGTSTP, SIGTTIN, SIGTTOU, SIGQUIT.
 * MUST be called in every child before exec, because dispositions set to
 * SIG_IGN survive execvp().
 *
 * CRITERIA: "Ctrl-c must quit current foreground process" and "Ctrl-z must send
 * SIGTSTP to the current foreground process" -- without this reset the child
 * inherits the shell's SIG_IGN and would ignore both.
 */
void sig_reset_child(void);

/*
 * tcsetpgrp(shell_terminal, pgid). Safe to call from the shell because
 * sig_init_shell() already ignores SIGTTOU.
 *
 * CRITERIA: makes the job the foreground process group so the tty sends Ctrl-c
 * and Ctrl-z to it; called with the job's pgid before waiting and with
 * shell_pgid after the job exits or stops. Background jobs never get it, which
 * is what keeps `&` jobs immune to Ctrl-c.
 */
void sig_give_terminal(pid_t pgid);

#endif /* SIG_H */
