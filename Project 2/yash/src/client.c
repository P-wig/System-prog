#include "client.h"

#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/*
 * Set by the handlers, acted on by the main loop. Writing to the socket from
 * inside a handler would not be async-signal-safe, so the handlers do nothing
 * but raise a flag.
 */
static volatile sig_atomic_t got_int;
static volatile sig_atomic_t got_stop;

int client_connect_tcp(const char *ip)
{
    struct sockaddr_in addr;
    int                fd;

    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(YASH_PORT);   /* never an argument */

    if (inet_pton(AF_INET, ip, &addr.sin_addr) != 1) {
        fprintf(stderr, "yash: not an IPv4 address: %s\n", ip);
        return -1;
    }

    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror(ip);
        close(fd);
        return -1;
    }
    return fd;
}

int client_connect_unix(const char *path)
{
    struct sockaddr_un addr;
    int                fd;

    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path, sizeof addr.sun_path - 1);

    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        perror("socket");
        return -1;
    }

    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        perror(path);
        close(fd);
        return -1;
    }
    return fd;
}

static void on_signal(int signo)
{
    if (signo == SIGINT)
        got_int = 1;
    else
        got_stop = 1;
}

void client_install_handlers(void)
{
    struct sigaction sa;

    sa.sa_handler = on_signal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;    /* no SA_RESTART: pselect must return EINTR */

    /* Catching SIGTSTP is also what stops Ctrl-z from suspending the client
     * itself; the keystroke belongs to the command on the server. */
    sigaction(SIGINT,  &sa, NULL);
    sigaction(SIGTSTP, &sa, NULL);

    /* The server may vanish between our select and our write. */
    signal(SIGPIPE, SIG_IGN);
}

static int send_all(int fd, const char *buf, size_t n)
{
    size_t off = 0;

    while (off < n) {
        ssize_t w = write(fd, buf + off, n - off);

        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        off += (size_t)w;
    }
    return 0;
}

static int send_msg(int fd, const char *prefix, const char *body)
{
    char buf[YASH_BUF_SIZE];
    int  n = snprintf(buf, sizeof buf, "%s%s\n", prefix, body);

    if (n <= 0)
        return 0;
    if ((size_t)n >= sizeof buf)
        n = (int)sizeof buf - 1;

    return send_all(fd, buf, (size_t)n);
}

/*
 * The prompt is the only framing in the protocol, so the client tracks the
 * last three bytes it received rather than assuming one read per message.
 */
static void push_tail(char tail[3], const char *buf, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++) {
        tail[0] = tail[1];
        tail[1] = tail[2];
        tail[2] = buf[i];
    }
}

static int at_prompt(const char tail[3])
{
    return tail[0] == '\n' && tail[1] == '#' && tail[2] == ' ';
}

/*
 * One typed line. At the prompt it is a command; while a command runs it is
 * that command's stdin. Returns 1 when the user asked to quit.
 */
static int send_line(int sockfd, const char *line, int *prompt)
{
    if (*prompt) {
        if (strcmp(line, "quit") == 0)
            return 1;
        send_msg(sockfd, MSG_CMD, line);
        /* Nothing else is typed as a command until the prompt comes back. */
        *prompt = 0;
    } else {
        send_msg(sockfd, "", line);
    }
    return 0;
}

int client_run(int sockfd)
{
    char     tail[3] = { '\n', '#', ' ' };  /* assume a prompt until told otherwise */
    char     inbuf[YASH_BUF_SIZE];
    size_t   inlen = 0;
    int      prompt = 1;
    int      stdin_open = 1;
    int      done = 0;
    sigset_t block, orig, empty;

    /*
     * Block the two job-control signals and unblock them only inside
     * pselect. Otherwise a Ctrl-c arriving just before the call would set the
     * flag and then leave us blocked until the next byte of unrelated input.
     */
    sigemptyset(&block);
    sigaddset(&block, SIGINT);
    sigaddset(&block, SIGTSTP);
    sigprocmask(SIG_BLOCK, &block, &orig);
    sigemptyset(&empty);

    while (!done) {
        fd_set rfds;
        int    maxfd = sockfd;
        int    r;

        if (got_int) {
            got_int = 0;
            send_msg(sockfd, MSG_CTL, "c");
        }
        if (got_stop) {
            got_stop = 0;
            send_msg(sockfd, MSG_CTL, "z");
        }

        FD_ZERO(&rfds);
        FD_SET(sockfd, &rfds);
        if (stdin_open) {
            FD_SET(STDIN_FILENO, &rfds);
            if (STDIN_FILENO > maxfd)
                maxfd = STDIN_FILENO;
        }

        r = pselect(maxfd + 1, &rfds, NULL, NULL, NULL, &empty);
        if (r < 0) {
            if (errno == EINTR)
                continue;       /* a flag was raised; handle it at the top */
            break;
        }

        if (FD_ISSET(sockfd, &rfds)) {
            char    buf[YASH_BUF_SIZE];
            ssize_t n = read(sockfd, buf, sizeof buf);

            if (n <= 0) {
                if (n < 0 && errno == EINTR)
                    continue;
                break;          /* server gone */
            }
            /* Printed verbatim, prompt included: the client never parses the
             * server's output and never prints a prompt of its own. */
            if (send_all(STDOUT_FILENO, buf, (size_t)n) < 0)
                break;
            push_tail(tail, buf, (size_t)n);
            prompt = at_prompt(tail);
        }

        if (stdin_open && FD_ISSET(STDIN_FILENO, &rfds)) {
            ssize_t n = read(STDIN_FILENO, inbuf + inlen, sizeof inbuf - inlen);
            char   *nl;

            if (n < 0) {
                if (errno == EINTR)
                    continue;
                break;
            }

            if (n == 0) {
                /*
                 * Ctrl-d. Anything typed before it on the same line is
                 * delivered first, then the EOF itself.
                 */
                if (inlen > 0) {
                    inbuf[inlen] = '\0';
                    done = send_line(sockfd, inbuf, &prompt);
                    inlen = 0;
                }
                send_msg(sockfd, MSG_CTL, "d");
                if (prompt)
                    done = 1;           /* at the prompt it ends the session */
                else if (!isatty(STDIN_FILENO))
                    stdin_open = 0;     /* a real EOF would spin the loop */
                continue;
            }

            inlen += (size_t)n;

            while (!done && (nl = memchr(inbuf, '\n', inlen)) != NULL) {
                size_t len = (size_t)(nl - inbuf);

                *nl = '\0';
                done = send_line(sockfd, inbuf, &prompt);

                memmove(inbuf, nl + 1, inlen - len - 1);
                inlen -= len + 1;
            }

            if (inlen == sizeof inbuf)  /* overlong line: flush what we have */
                inlen = 0;
        }
    }

    sigprocmask(SIG_SETMASK, &orig, NULL);
    close(sockfd);
    write(STDOUT_FILENO, "\n", 1);
    return 0;
}
