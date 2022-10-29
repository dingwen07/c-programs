#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <arpa/inet.h>

#include "buffer.h"

#define RECV_BUF_SIZE 1024

int main(int argc, char *argv[])
{
    int sockfd, portno, n, c;
    struct addrinfo hints, *servinfo;
    buffer_t *serv_ip = buffer_new();
    buffer_t *clin_buf = buffer_new();

    // Parse command line arguments
    // <server ip> <port>

    if (argc < 3) {
        printf("Enter server IP: ");
        buffer_getline(STDIN_FILENO, serv_ip);
        serv_ip->len--;
        buffer_null_terminate(serv_ip);
        printf("Enter port number: ");
        scanf("%d", &portno);
    } else {
        buffer_append(serv_ip, argv[1], strlen(argv[1]));
        portno = atoi(argv[2]);
    }
    buffer_inspect(serv_ip, BUF_INSP_ALL);

    // get address info
    memset(&hints, 0, sizeof (hints));
    hints.ai_family = PF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int err = getaddrinfo((char *)serv_ip->data, NULL, &hints, &servinfo);
    if (err != 0) {
        perror("ERROR getaddrinfo");
        exit(1);
    }

    // print address
    buffer_t *ipstr = buffer_new();
    buffer_ensure(ipstr, INET6_ADDRSTRLEN);
    inet_ntop(servinfo->ai_family, servinfo->ai_addr->sa_data, (char *)ipstr->data, ipstr->capacity);
    ipstr->len = strlen((char *)ipstr->data);
    buffer_null_terminate(ipstr);
    buffer_inspect(ipstr, BUF_INSP_ALL);

    printf("Trying %s...\n", (char *)ipstr->data);

    // create socket
    if (servinfo->ai_family == AF_INET) {
        struct sockaddr_in *ipv4 = (struct sockaddr_in *)servinfo->ai_addr;
        ipv4->sin_port = htons(portno);
    } else {
        struct sockaddr_in6 *ipv6 = (struct sockaddr_in6 *)servinfo->ai_addr;
        ipv6->sin6_port = htons(portno);
    }

    sockfd = socket(servinfo->ai_family, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }
    
    // connect to server
    if (connect(sockfd, servinfo->ai_addr, servinfo->ai_addrlen) < 0) {
        perror("ERROR connecting");
        exit(1);
    }

    fprintf(stderr, "Connected to to %s.\n", (char *)serv_ip->data);


    int stdin_eof = 0;

    while (1) {
        // select on stdin and sockfd
        fd_set readfds;
        FD_ZERO(&readfds);
        if (stdin_eof == 0) {
            FD_SET(STDIN_FILENO, &readfds);
        }
        FD_SET(sockfd, &readfds);
        fprintf(stderr, "selecting...\n");
        select(sockfd + 1, &readfds, NULL, NULL, NULL);
        // get fd with data
        int readfd;
        if (FD_ISSET(STDIN_FILENO, &readfds) && stdin_eof == 0) {
            readfd = STDIN_FILENO;
        } else if (FD_ISSET(sockfd, &readfds)) {
            readfd = sockfd;
        } else {
            continue;
        }
        // read data from fd
        if (readfd == STDIN_FILENO) {
            fprintf(stderr, "Reading from stdin...\n");
            n = buffer_read(STDIN_FILENO, clin_buf, 1);
            if (n == 0) {
                // fprintf(stderr, "STDIN EOF\n");
                stdin_eof = 1;
                // shutdown(sockfd, SHUT_WR);
                continue;
            }
            if (((char *)clin_buf->data)[clin_buf->len - 1] == '\n') {
                n = buffer_write(sockfd, clin_buf);
                buffer_reset(clin_buf);
            }
        } else if (readfd == sockfd) {
            fprintf(stderr, "Reading from socket...\n");
            n = read(sockfd, &c, 1);
            if (n == 0) {
                fprintf(stderr, "Server closed connection\n");
                break;
            }
            fputc(c, stdout);
        }
    }
}
