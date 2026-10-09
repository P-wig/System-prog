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
    /* TODO: open(YASHD_LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644). */
    return -1;
}

/*
 *   Sep 18 10:02:55 yashd[127.0.0.1:7813]: ls -l
 *
 * Formatted whole into a local buffer and emitted with a single write, so
 * concurrent threads cannot interleave fragments of different entries.
 * localtime_r, not localtime: the latter returns a shared static struct.
 */
void log_command(const peer_t *peer, const char *cmdline)
{
    /* TODO: strftime("%b %e %H:%M:%S"), snprintf the entry, lock, write, unlock. */
    (void)peer;
    (void)cmdline;
    (void)log_fd;
    (void)log_mutex;
}

void log_close(void)
{
    /* TODO: close(log_fd) if open. */
}
