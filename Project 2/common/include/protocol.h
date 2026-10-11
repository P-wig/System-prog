#ifndef PROTOCOL_H
#define PROTOCOL_H

/*
 * Wire constants shared by yashd and yash. Nothing here depends on the
 * transport, so the same values apply over AF_UNIX and AF_INET.
 * See PROTOCOL.md.
 */

#define YASH_PORT 3820                /* hardcoded on both sides */
#define YASHD_SOCK_PATH "/tmp/yashd.sock"   /* AF_UNIX development mode */

/* Client -> server message keywords, each followed by one blank. */
#define MSG_CMD "CMD "
#define MSG_CTL "CTL "
#define MSG_PREFIX_LEN 4

/* The single character carried by a CTL message. */
#define CTL_INT 'c'   /* interrupt the foreground job */
#define CTL_STOP 'z'   /* stop the foreground job */
#define CTL_EOF 'd'   /* EOF on the running command's stdin */

/* Leading newline keeps the prompt on a line of its own no matter how the
 * previous command's output ended. The client prints it verbatim. */
#define YASH_PROMPT "\n# "

#define YASH_MAX_LINE 200
#define YASH_BUF_SIZE 4096

#endif /* PROTOCOL_H */
