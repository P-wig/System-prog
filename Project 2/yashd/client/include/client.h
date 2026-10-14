#ifndef CLIENT_H
#define CLIENT_H

#include "protocol.h"

/*
 * The client holds no shell state. It connects, pumps bytes between the
 * terminal and the socket, and turns Ctrl-c / Ctrl-z / Ctrl-d into CTL
 * messages. The port is never an argument; it is YASH_PORT.
 */

int client_connect_tcp(const char *ip);
int client_connect_unix(const char *path);

/* SIGINT/SIGTSTP handlers that only set a flag, installed without SA_RESTART
 * so select() returns EINTR and the loop notices promptly. */
void client_install_handlers(void);

/* select() loop over stdin and the socket; returns the process exit status. */
int client_run(int sockfd);

#endif /* CLIENT_H */
