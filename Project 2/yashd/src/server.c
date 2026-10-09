#include "server.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "session.h"
#include "transport.h"

#define BACKLOG 16

int server_run(transport_kind_t kind)
{
    /*
     * TODO:
     *   lfd = transport_listen(kind);
     *   for (;;) {
     *       cfd = transport_accept(lfd, kind, &peer);
     *       s   = session_create(cfd, &peer);
     *       pthread_create(&tid, NULL, session_main, s);
     *       pthread_detach(tid);   -- nothing ever joins a session thread
     *   }
     */
    (void)kind;
    return -1;
}
