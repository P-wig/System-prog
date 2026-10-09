#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <netinet/in.h>

/*
 * The only module that knows which socket family is in use. Everything above
 * accept() works on a plain connected fd.
 */

typedef enum {
    TRANSPORT_UNIX,   /* /tmp/yashd.sock, local development */
    TRANSPORT_TCP     /* INADDR_ANY:3820, the graded mode */
} transport_kind_t;

/* Peer identity for the log line. An AF_UNIX peer has no address, so the
 * fields hold a stand-in rather than being left empty. */
typedef struct {
    char ip[INET_ADDRSTRLEN];
    int  port;
} peer_t;

/* Returns a bound, listening fd, or -1. */
int transport_listen(transport_kind_t kind);

/* Blocks until a client connects; fills *peer. Returns the connected fd, or -1. */
int transport_accept(int lfd, transport_kind_t kind, peer_t *peer);

/* Closes the listening fd and unlinks sun_path when kind is TRANSPORT_UNIX. */
void transport_close(int lfd, transport_kind_t kind);

#endif /* TRANSPORT_H */
