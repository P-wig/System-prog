#include "daemon.h"

#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "yashd.h"

#define MAX_FD_SCAN 4096

/* Held open for the daemon's lifetime: closing it would release the lock. */
static int pid_fd = -1;

static int pidfile_lock(void)
{
    char buf[32];
    int  n;

    pid_fd = open(YASHD_PID_PATH, O_RDWR | O_CREAT, 0600);
    if (pid_fd < 0)
        return -1;

    /* Record locks are not inherited across fork, so this must run in the
     * final daemon process, after both forks. */
    if (lockf(pid_fd, F_TLOCK, 0) < 0) {
        close(pid_fd);
        pid_fd = -1;
        return -1;
    }

    if (ftruncate(pid_fd, 0) < 0)
        return -1;

    n = snprintf(buf, sizeof buf, "%ld\n", (long)getpid());
    if (n > 0 && write(pid_fd, buf, (size_t)n) != n)
        return -1;

    return 0;
}

int daemon_init(void)
{
    pid_t pid;
    long  maxfd;
    long  fd;

    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(0);           /* the child is not a group leader, so setsid works */

    if (setsid() < 0)       /* new session, new group, no controlling terminal */
        return -1;

    /* A second fork leaves us not a session leader, so opening a tty later
     * can never reacquire one. */
    signal(SIGHUP, SIG_IGN);
    pid = fork();
    if (pid < 0)
        return -1;
    if (pid > 0)
        _exit(0);

    /*
     * /tmp rather than / so a client's relative-path redirection
     * (`echo hi > out.txt`) lands somewhere writable. The point of the chdir
     * is only to stop pinning the invoking user's directory.
     */
    if (chdir("/tmp") < 0)
        return -1;

    umask(0);

    maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > MAX_FD_SCAN)
        maxfd = MAX_FD_SCAN;
    for (fd = 0; fd < maxfd; fd++)
        close((int)fd);

    /*
     * Reopen the standard three on /dev/null. Left closed, the first socket
     * we open would become descriptor 1 and any stray printf in the ported
     * shell code would be injected straight into a client's stream.
     */
    if (open("/dev/null", O_RDWR) != STDIN_FILENO)
        return -1;
    if (dup(STDIN_FILENO) != STDOUT_FILENO)
        return -1;
    if (dup(STDIN_FILENO) != STDERR_FILENO)
        return -1;

    return pidfile_lock();
}
