#include "daemon.h"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "yashd.h"

/* Takes an exclusive lock on YASHD_PID_PATH and writes our pid into it. */
static int pidfile_lock(void)
{
    /* TODO: open(O_RDWR|O_CREAT, 0600), lockf(F_TLOCK), ftruncate, write pid. */
    return -1;
}

int daemon_init(void)
{
    /*
     * TODO:
     *  1. fork(); parent _exit(0)            -- child is not a group leader
     *  2. setsid()                           -- new session, no controlling tty
     *  3. ignore SIGHUP, fork() again        -- can never reacquire a tty
     *  4. chdir("/"); umask(0)
     *  5. close every inherited descriptor
     *  6. reopen 0, 1, 2 on /dev/null        -- a stray printf must not land
     *                                           in a descriptor later reused
     *                                           for a client socket
     *  7. pidfile_lock()                     -- single instance
     */
    (void)pidfile_lock;
    return -1;
}
