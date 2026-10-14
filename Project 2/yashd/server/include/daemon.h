#ifndef DAEMON_H
#define DAEMON_H

/*
 * fork / setsid / fork / chdir / umask / close fds / reopen 0,1,2 on
 * /dev/null, then take an exclusive lock on YASHD_PID_PATH so a second
 * instance fails immediately. Returns 0 in the daemon, -1 on failure.
 */
int daemon_init(void);

#endif /* DAEMON_H */
