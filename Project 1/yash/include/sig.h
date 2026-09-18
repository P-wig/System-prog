#ifndef SIG_H
#define SIG_H

#include <sys/types.h>

/*
 * Signal dispositions and terminal ownership.
 *
 * The whole Ctrl-c / Ctrl-z section of the spec reduces to one rule: the
 * terminal delivers SIGINT/SIGTSTP to the *foreground process group*. So the
 * shell never has to forward anything; it only needs to (a) make those signals
 * harmless to itself and (b) make sure the foreground job, not the shell, owns
 * the terminal while it runs.
 */

extern pid_t shell_pgid;
extern int   shell_terminal;   /* STDIN_FILENO */

/*
 * Puts the shell in its own process group, takes the terminal, and neutralizes
 * the job-control signals.
 *
 * CRITERIA:
 *  - "Ctrl-c ... [must] not [quit] the shell"   -> SIGINT gets a no-op handler.
 *  - "The shell will not be stopped on SIGTSTP" -> SIGTSTP gets a no-op handler.
 *  - "should not print the process (unlike bash)" -> the handlers are empty, so
 *    there is nothing that could print a job line.
 *  - SIGTTIN/SIGTTOU are SIG_IGN so the shell's own tcsetpgrp() cannot stop it.
 *
 * A no-op handler is used instead of SIG_IGN for SIGINT/SIGTSTP because it is
 * installed without SA_RESTART: a Ctrl-c typed at the prompt then interrupts
 * fgets with EINTR, letting main() reprompt on a fresh line. SIG_IGN would not
 * interrupt the read at all.
 *
 * SIGCHLD is deliberately left at SIG_DFL; see jobs_reap().
 */
void sig_init_shell(void);

/*
 * Restores SIG_DFL for SIGINT, SIGTSTP, SIGTTIN, SIGTTOU, SIGQUIT.
 * MUST be called in every child before exec. exec resets handlers on its own,
 * but SIG_IGN survives it, so SIGTTIN/SIGTTOU would stay ignored without this.
 *
 * CRITERIA: "Ctrl-c must quit current foreground process" and "Ctrl-z must send
 * SIGTSTP to the current foreground process" -- the child needs the default
 * terminate/stop behavior for those two signals.
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
