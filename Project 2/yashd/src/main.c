#include <stdio.h>
#include <string.h>

#include "daemon.h"
#include "log.h"
#include "server.h"
#include "transport.h"
#include "yashd.h"

/*
 * $ yashd        TCP on YASH_PORT, the graded mode
 * $ yashd -u     AF_UNIX on YASHD_SOCK_PATH, for local development
 */
int main(int argc, char *argv[])
{
    transport_kind_t kind = TRANSPORT_TCP;

    if (argc == 2 && strcmp(argv[1], "-u") == 0) {
        kind = TRANSPORT_UNIX;
    } else if (argc != 1) {
        fprintf(stderr, "usage: %s [-u]\n", argv[0]);
        return 1;
    }

    /* TODO: daemon_init(), log_open(), ignore SIGPIPE, then server_run(kind). */
    (void)kind;
    return 0;
}
