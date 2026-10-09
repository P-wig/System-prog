#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "client.h"

/*
 * $ yash <IP_Address_of_Server>   TCP, port is always YASH_PORT
 * $ yash -u                       AF_UNIX on YASHD_SOCK_PATH, for development
 */
int main(int argc, char *argv[])
{
    int sockfd;

    if (argc != 2) {
        fprintf(stderr, "usage: %s <server-ip> | -u\n", argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-u") == 0)
        sockfd = client_connect_unix(YASHD_SOCK_PATH);
    else
        sockfd = client_connect_tcp(argv[1]);

    if (sockfd < 0)
        return 1;

    client_install_handlers();
    return client_run(sockfd);
}
