#include "log.h"

#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "yashd.h"

static int             log_fd = -1;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

int log_open(void)
{
    log_fd = open(YASHD_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    return log_fd < 0 ? -1 : 0;
}

/*
 *   Sep 18 10:02:55 yashd[127.0.0.1:7813]: ls -l
 *
 * The whole entry is formatted into a local buffer and emitted with a single
 * write, so two session threads cannot interleave fragments of different
 * lines. localtime_r, not localtime: the latter returns a shared static.
 */
void log_command(const peer_t *peer, const char *cmdline)
{
    char      stamp[32];
    char      entry[YASH_MAX_LINE + 128];
    struct tm tm;
    time_t    now;
    ssize_t   written;
    int       n;

    if (log_fd < 0 || cmdline == NULL || *cmdline == '\0')
        return;

    now = time(NULL);
    if (localtime_r(&now, &tm) == NULL)
        return;
    if (strftime(stamp, sizeof stamp, "%b %e %H:%M:%S", &tm) == 0)
        return;

    n = snprintf(entry, sizeof entry, "%s yashd[%s:%d]: %s\n",
                 stamp, peer->ip, peer->port, cmdline);
    if (n <= 0)
        return;
    if ((size_t)n >= sizeof entry)
        n = (int)sizeof entry - 1;

    pthread_mutex_lock(&log_mutex);
    written = write(log_fd, entry, (size_t)n);
    pthread_mutex_unlock(&log_mutex);
    (void)written;
}

void log_close(void)
{
    if (log_fd >= 0) {
        close(log_fd);
        log_fd = -1;
    }
}
