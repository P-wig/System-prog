#ifndef SIG_H
#define SIG_H

#include <sys/types.h>

extern pid_t shell_pgid;
extern int   shell_terminal;

/* Puts the shell in its own process group and ignores job-control signals. */
void sig_init_shell(void);

/* Restores default dispositions; call in every child before exec. */
void sig_reset_child(void);

/* Hands the terminal to pgid (SIGTTOU-safe). */
void sig_give_terminal(pid_t pgid);

#endif /* SIG_H */
