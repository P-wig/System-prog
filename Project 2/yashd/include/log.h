#ifndef LOG_H
#define LOG_H

#include "transport.h"

/*
 * /tmp/yashd.log, written by every session thread:
 *   Sep 18 10:02:55 yashd[127.0.0.1:7813]: ls -l
 * Each entry is formatted into a local buffer and emitted with one write()
 * under a mutex, so concurrent threads cannot interleave fragments.
 */

int  log_open(void);
void log_command(const peer_t *peer, const char *cmdline);
void log_close(void);

#endif /* LOG_H */
