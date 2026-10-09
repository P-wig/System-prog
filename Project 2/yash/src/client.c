#include "client.h"

#include <arpa/inet.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* Set by the handlers, acted on by the select loop. */
static volatile sig_atomic_t got_int;
static volatile sig_atomic_t got_stop;

int client_connect_tcp(const char *ip)
{
    /* TODO: socket(AF_INET, SOCK_STREAM, 0); inet_pton(ip);
     *       connect to htons(YASH_PORT). */
    (void)ip;
    return -1;
}

int client_connect_unix(const char *path)
{
    /* TODO: socket(AF_UNIX, SOCK_STREAM, 0); connect to path. */
    (void)path;
    return -1;
}

static void on_signal(int signo)
{
    if (signo == SIGINT)
        got_int = 1;
    else
        got_stop = 1;
}

void client_install_handlers(void)
{
    /* TODO: sigaction for SIGINT and SIGTSTP with sa_flags = 0.
     *       No SA_RESTART, so select() returns EINTR and the loop sees the
     *       flag instead of blocking until the next byte arrives. */
    (void)on_signal;
}

int client_run(int sockfd)
{
    /*
     * TODO: one select() over stdin and sockfd.
     *
     *   socket readable -> read and write the bytes to stdout verbatim,
     *                      including the "\n# " prompt; 0 means the server
     *                      is gone. Track at_prompt from the trailing bytes.
     *   stdin readable  -> at_prompt ? send "CMD <line>\n" : send the line raw
     *                      EOF (Ctrl-d) -> send "CTL d\n"; exit if at_prompt
     *   got_int         -> send "CTL c\n", clear the flag
     *   got_stop        -> send "CTL z\n", clear the flag
     *
     * The client never prints a prompt of its own and never parses the
     * server's output.
     */
    (void)sockfd;
    (void)got_int;
    (void)got_stop;
    return 1;
}
