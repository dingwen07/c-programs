#include <stdio.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <pthread.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#define MAX_BUFFER_SIZE 4096

void print_usage();
void *handle_connection(void* args);

struct connection {
    int sockfd;
    struct sockaddr_in cli_addr;
};
char *remote_host = NULL;
int remote_port = 0;
int quiet = 0;


int main(int argc, char *argv[]) {
    // get options
    // -p local port (the port to listen on)
    // -R remote host (the host to forward to)
    // -P remote port (the port to forward to)
    // -q quiet (don't print data)
    int local_port = 0;
    int c;
    while ((c = getopt(argc, argv, "p:R:P:q")) != -1) {
        switch (c) {
            case 'p':
                local_port = atoi(optarg);
                break;
            case 'R':
                remote_host = optarg;
                break;
            case 'P':
                remote_port = atoi(optarg);
                break;
            case 'q':
                quiet = 1;
                break;
            default:
                abort();
        }
    }
    // if any of the options are missing, exit
    if (local_port == 0 || remote_host == NULL || remote_port == 0) {
        print_usage();
        return 1;
    }

    // listen on local port
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        perror("ERROR opening socket");
        exit(1);
    }
    struct sockaddr_in serv_addr;
    bzero((char *) &serv_addr, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(local_port);
    if (bind(sockfd, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("ERROR on binding");
        exit(1);
    }
    listen(sockfd, 10);

    // accept connections
    struct sockaddr_in cli_addr;
    socklen_t clilen = sizeof(cli_addr);
    printf("Listening on port %d\n", local_port);
    while (1) {
        int newsockfd = accept(sockfd, (struct sockaddr *) &cli_addr, &clilen);
        if (newsockfd < 0) {
            perror("ERROR on accept");
            exit(1);
        }
        // create a new thread to handle the connection
        pthread_t thread;
        struct connection *conn = malloc(sizeof(struct connection));
        conn->sockfd = newsockfd;
        conn->cli_addr = cli_addr;
        pthread_create(&thread, NULL, handle_connection, conn);
    }
}


void *handle_connection(void* args) {
    struct connection *conn = (struct connection *) args;
    int sockfd = conn->sockfd;
    struct sockaddr_in cli_addr = conn->cli_addr;
    free(conn);

    // print client address
    printf("Client: (%s:%d) connected\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

    // connect to remote host
    int remote_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (remote_sockfd < 0) {
        perror("ERROR opening socket");
        shutdown(sockfd, SHUT_RDWR);
        pthread_exit(NULL);
    }
    struct sockaddr_in remote_addr;
    bzero((char *) &remote_addr, sizeof(remote_addr));
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(remote_port);
    if (inet_pton(AF_INET, remote_host, &remote_addr.sin_addr) <= 0) {
        perror("ERROR on inet_pton");
        shutdown(sockfd, SHUT_RDWR);
        pthread_exit(NULL);
    }
    if (connect(remote_sockfd, (struct sockaddr *) &remote_addr, sizeof(remote_addr)) < 0) {
        perror("ERROR on connect to remote host");
        shutdown(sockfd, SHUT_RDWR);
        pthread_exit(NULL);
    }
    // forward data
    char buffer[MAX_BUFFER_SIZE];
    int n;
    while (1) {
        // use select to check if data is available on either socket
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);
        FD_SET(remote_sockfd, &readfds);
        int maxfd = sockfd > remote_sockfd ? sockfd : remote_sockfd;
        int retval = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (retval == -1) {
            perror("ERROR on select");
            close(sockfd);
            close(remote_sockfd);
            pthread_exit(NULL);
        } else if (retval) {
            // data is available on one of the sockets
            int from_sockfd;
            int to_sockfd;
            if (FD_ISSET(sockfd, &readfds)) {
                from_sockfd = sockfd;
                to_sockfd = remote_sockfd;
            } else {
                from_sockfd = remote_sockfd;
                to_sockfd = sockfd;
            }
            n = read(from_sockfd, buffer, MAX_BUFFER_SIZE);
            if (n < 0) {
                perror("ERROR reading from socket");
                close(sockfd);
                close(remote_sockfd);
                pthread_exit(NULL);
            }
            if (n == 0) {
                // connection closed
                // print
                printf("Client: (%s:%d) disconnected\n", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
                close(sockfd);
                close(remote_sockfd);
                pthread_exit(NULL);
            }
            n = write(to_sockfd, buffer, n);
            if (n < 0) {
                perror("ERROR writing to socket");
                close(sockfd);
                close(remote_sockfd);
                pthread_exit(NULL);
            }
            // print data
            printf("Client: (%s:%d) ", inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));
            if (from_sockfd == sockfd) {
                // data from client
                printf("->");
            } else {
                // data from remote host
                printf("<-");
            }
            printf(" Remote: (%s:%d): %d Bytes\n", remote_host, remote_port, n);
            // print data using xxd
            // pipe data to xxd, then print the output
            // set timeout to 1 second using timeout command
            if (!quiet) {
                FILE *fp = popen("timeout 1 xxd", "w");
                fwrite(buffer, 1, n, fp);
                pclose(fp);
            }
        }
    }
}


void print_usage() {
    fprintf(stderr, "Usage: tcpfwd -p local_port -R remote_host -P remote_port [-q]\n");
}
