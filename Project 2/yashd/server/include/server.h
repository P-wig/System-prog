#ifndef SERVER_H
#define SERVER_H

#include "transport.h"

/*
 * Listens, then accepts forever, spawning one detached pthread per client.
 * Does not return under normal operation.
 */
int server_run(transport_kind_t kind);

#endif /* SERVER_H */
