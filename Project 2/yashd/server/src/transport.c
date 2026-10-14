#define _GNU_SOURCE   /* struct ucred, SO_PEERCRED */

#include "transport.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "yashd.h"

/*
 * Without SO_REUSEADDR a restart inside the TIME_WAIT window fails with
 * EADDRINUSE, which is exactly what happens when the daemon is killed and
 * relaunched during testing.
 */
static int reusePort(int sockfd)
{
    int one = 1;

    return setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
}

static int listen_unix(void)
{
    struct sockaddr_un addr;
    int                fd;

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    unlink(YASHD_SOCK_PATH);   /* a stale node from an unclean exit blocks bind */

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, YASHD_SOCK_PATH, sizeof addr.sun_path - 1);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0 ||
        listen(fd, YASHD_BACKLOG) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static int listen_tcp(void)
{
    struct sockaddr_in addr;
    int                fd;

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0)
        return -1;

    if (reusePort(fd) < 0) {
        close(fd);
        return -1;
    }

    memset(&addr, 0, sizeof addr);
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(YASH_PORT);

    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0 ||
        listen(fd, YASHD_BACKLOG) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

int transport_listen(transport_kind_t kind)
{
    return kind == TRANSPORT_UNIX ? listen_unix() : listen_tcp();
}

/* An AF_UNIX client has no address, so its pid stands in for the port. */
static void peer_from_unix(int cfd, peer_t *peer)
{
    struct ucred cred;
    socklen_t    len = sizeof cred;

    strcpy(peer->ip, "local");
    peer->port = 0;

    if (getsockopt(cfd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0)
        peer->port = (int)cred.pid;
}

static void peer_from_tcp(const struct sockaddr_in *sin, peer_t *peer)
{
    if (inet_ntop(AF_INET, &sin->sin_addr, peer->ip, sizeof peer->ip) == NULL)
        strcpy(peer->ip, "?");
    peer->port = ntohs(sin->sin_port);
}

int transport_accept(int lfd, transport_kind_t kind, peer_t *peer)
{
    struct sockaddr_storage from;
    socklen_t               len = sizeof from;
    int                     cfd;

    cfd = accept(lfd, (struct sockaddr *)&from, &len);
    if (cfd < 0)
        return -1;

    memset(peer, 0, sizeof *peer);
    if (kind == TRANSPORT_UNIX)
        peer_from_unix(cfd, peer);
    else
        peer_from_tcp((struct sockaddr_in *)&from, peer);

    return cfd;
}

void transport_close(int lfd, transport_kind_t kind)
{
    if (lfd >= 0)
        close(lfd);
    if (kind == TRANSPORT_UNIX)
        unlink(YASHD_SOCK_PATH);
}
