#include "server.h"

#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "session.h"
#include "transport.h"

int server_run(transport_kind_t kind)
{
    int lfd = transport_listen(kind);

    if (lfd < 0)
        return -1;

    for (;;) {
        peer_t     peer;
        session_t *s;
        pthread_t  tid;
        int        cfd = transport_accept(lfd, kind, &peer);

        if (cfd < 0) {
            if (errno == EINTR || errno == ECONNABORTED)
                continue;
            break;
        }

        s = session_create(cfd, &peer);
        if (s == NULL) {
            close(cfd);
            continue;
        }

        /* Nothing ever joins a session thread, so detaching is what reclaims
         * its stack when the client disconnects. */
        if (pthread_create(&tid, NULL, session_main, s) != 0) {
            session_destroy(s);
            continue;
        }
        pthread_detach(tid);
    }

    transport_close(lfd, kind);
    return -1;
}
