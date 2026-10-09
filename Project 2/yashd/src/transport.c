#include "transport.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "yashd.h"

/*
 * SO_REUSEADDR. Without it a restart within the TIME_WAIT window fails with
 * EADDRINUSE, which is exactly what happens while testing.
 */
static int reusePort(int sockfd)
{
    /* TODO: setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one). */
    (void)sockfd;
    return -1;
}

static int listen_unix(void)
{
    /* TODO: socket(AF_UNIX, SOCK_STREAM, 0); unlink(YASHD_SOCK_PATH);
     *       bind to sun_path; listen(). */
    return -1;
}

static int listen_tcp(void)
{
    /* TODO: socket(AF_INET, SOCK_STREAM, 0); reusePort();
     *       bind INADDR_ANY:YASH_PORT; listen(). */
    (void)reusePort;
    return -1;
}

int transport_listen(transport_kind_t kind)
{
    return kind == TRANSPORT_UNIX ? listen_unix() : listen_tcp();
}

int transport_accept(int lfd, transport_kind_t kind, peer_t *peer)
{
    /*
     * TODO: accept() into a struct sockaddr_storage, then fill *peer.
     *   TCP  -> inet_ntop(AF_INET, &sin.sin_addr, ...) and ntohs(sin.sin_port)
     *   UNIX -> no address exists; use SO_PEERCRED for a "local:<pid>" stand-in
     *           so log_command() stays one code path.
     */
    (void)lfd;
    (void)kind;
    (void)peer;
    return -1;
}

void transport_close(int lfd, transport_kind_t kind)
{
    /* TODO: close(lfd), and unlink(YASHD_SOCK_PATH) when kind is UNIX. */
    (void)lfd;
    (void)kind;
}
