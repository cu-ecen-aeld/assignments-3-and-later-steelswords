#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
#include <semaphore.h>
#include <errno.h>
#include <stdlib.h>
#include "signal_handle.h"

// We need at least C11 to use stdatomic.h
#ifndef __STDC_VERSION__
#error "Expected __STDC_VERSION__ macro to be defined; it was not. Incompatible toolchain detected."
#endif
#if __STDC_VERSION__ < 2011L
#error "The server program requires at least the C11 standard. This version is too early. Incompatible toolchain detected."
#endif
#include <stdatomic.h>

#define MAX_BUF_SIZE (1024 * 1024)


void log_error(const char *msg)
{
    char *errno_reason = strerror(errno);
    fprintf(stderr, "%s: %s\n", msg, errno_reason);
    syslog(LOG_ERR, "%s: %s", msg, errno_reason);
}

int bind_to_localhost(int sockfd)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    struct addrinfo *res = malloc(sizeof(struct addrinfo));
    if (res == NULL)
    {
        log_error("Could not allocate memory for getting address of local port 9000");
        exit(2);
    }

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    int getaddrinfo_result = getaddrinfo(NULL, "9000", &hints, &res);
    if (getaddrinfo_result != 0)
    {
        const char *gai_error_str = gai_strerror(getaddrinfo_result);
        fprintf(stderr, "Could not get addrinfo for port 9000: %s\n",
                gai_error_str);
        syslog(LOG_ERR, "Could not get addrinfo for port 9000: %s",
                gai_error_str);
        exit(EXIT_FAILURE);
    }

    int bind_result = bind(sockfd, res->ai_addr, res->ai_addrlen);
    if (bind_result < 0)
    {
        log_error("Could not bind to socket");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int handle_connection(int sockfd)
{
    ssize_t total_bytes_read = 0;
    ssize_t this_round_bytes_read = 0;
    char *buf = malloc(MAX_BUF_SIZE);

    // Open file to write to.
    int file_mode = S_IWGRP | S_IWUSR | S_IRGRP | S_IRUSR;
    int diskfd = open("./aesdsocketdata", O_CREAT | O_APPEND | O_RDWR, file_mode);

    if (NULL == buf)
    {
        log_error("Could not allocate more memory for buffer");
        //exit(5);
    }

    while(-1 != (this_round_bytes_read = recv(sockfd, (void*)buf, MAX_BUF_SIZE, 0)))
    {
        if (0 == this_round_bytes_read)
        {
            printf("-> Socket closed.\n");
            break;
        }
        printf("-> Data recvd:\n-----------------------\n"
                "%s\n"
                "-----------------------\n",
            buf);
        write(diskfd, buf, this_round_bytes_read);
        free(buf);
        buf = malloc(MAX_BUF_SIZE);
    }
    free(buf);
    fsync(diskfd);
    close(diskfd);
    return 0;
}

int main(int argc, char** argv)
{
    printf("-> Setting up run flag\n");
    //int res = init_run_flag();
    int res = 0;
    if (res != 0)
    {
        log_error("Could not set up run flag.");
        exit(EXIT_FAILURE);
    }


    // Open socket
    printf("-> Opening socket\n");
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        log_error("Could not open socket");
        exit(EXIT_FAILURE);
    }
    // Set sockopt so the socket doesn't stay open past when the process exits
    int yes = 1;
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    // Bind to socket
    printf("-> Binding to socket\n");
    bind_to_localhost(sockfd);


    while (1 == get_run_flag())
    {
        printf("-> Listening for connections on socket.\n");
        int listen_res = listen(sockfd, SOMAXCONN);
        if (-1 == listen_res)
        {
            log_error("Could not listen to socket");
            exit(3);
        }

        struct sockaddr client_address;
        socklen_t client_address_length = 0;
        memset(&client_address, 0, sizeof(struct sockaddr));

        int connection_socket_fd = accept(sockfd, &client_address, &client_address_length);
        if (-1 == connection_socket_fd)
        {
            log_error("Could not accept connection");
        }
        // I don't think we need to deal with concurrent socket connections here,
        // at least the way I read the assignment requirements. So we will just
        // handle one at a time. The unit test will likely not do more than SOMAXCONN
        // connections simultaneously.
        int handle_result = handle_connection(connection_socket_fd);
        if (0 != handle_result)
        {
            fprintf(stderr, "Error handling connection from %s\n", client_address.sa_data);
            syslog(LOG_ERR, "Error handling connection from %s", client_address.sa_data);
        }
    }

    printf("-> Closing socket.\n");
    close(sockfd);
    printf("-> Exiting.\n");
}

