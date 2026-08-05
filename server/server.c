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

#define MAX_BUF_SIZE (4 * 1024)

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

int write_all(int fd, const char* buf, size_t len)
{
    ssize_t bytes_left = len;
    ssize_t bytes_written_this_round = 0;
    int fail_count = 0;
    const int MAX_FAIL_COUNT = 20;

    while (bytes_left > 0)
    {
        bytes_written_this_round = write(fd, (void*)buf, bytes_left);
        if (bytes_written_this_round <= 0)
        {
            log_error("Could not write to file");
            fail_count++;
            if (fail_count > MAX_FAIL_COUNT)
            {
                fprintf(stderr, "!! Could not write buf \"%s\" to fd %d. Quitting with %zd bytes to go. write() last returned %zd\n",
                        buf,
                        fd,
                        bytes_left,
                        bytes_written_this_round
                        );
                syslog(LOG_ERR, "Too many failures writing to fd %d. Quitting.", fd);
                return -1;
            }
            continue;
        }
        else
        {
            syslog(LOG_DEBUG, "Wrote %zd bytes to file", bytes_written_this_round);
            bytes_left = bytes_left - bytes_written_this_round;
            buf += bytes_written_this_round;
        }
    }
    return 0;
    syslog(LOG_DEBUG, "Finished writing all bytes to fd %d", fd);
}

int duplicate_data_across_fds(int input_fd, int output_fd)
{
    ssize_t num_bytes_read = 0;
    char buffer[MAX_BUF_SIZE];

    while(( num_bytes_read = read(input_fd, buffer, MAX_BUF_SIZE)) > 0)
    {
        if (-1 == write_all(output_fd, buffer, num_bytes_read))
        {
            log_error("Could not write all bytes to the buffer.");
            return -1;
        }
    }

    return 0;
}

void print_all(const char* msg)
{
    size_t len = strlen(msg);

    size_t num_newlines = 0;
    for (size_t i = 0; i < len; ++i)
    {
        if (msg[i] == '\n')
        {
            num_newlines++;
        }
    }

    size_t new_len = len + num_newlines;

    char* result = calloc(len, sizeof(char));


    for(size_t i = 0, j = 0; i < len; ++i, ++j)
    {
        if(msg[i] == '\n')
        {
            result[j++] = '\\';
            result[j] = 'n';
        }
        else {
            result[j] = msg[i];
        }
    }

    printf("->>%s<<-", result);
    free(result);
}

void spit_file_back_out_to_socket(int sockfd, int diskfd)
{
    // Rewind to beginning of file
    off_t seek_res = lseek(diskfd, 0, SEEK_SET);
    if (0 != seek_res)
    {
        log_error("Could not read from beginning of file");
        exit(13);
    }

    duplicate_data_across_fds(diskfd, sockfd);

#if 0
    ssize_t bytes_read = 0;
    while (get_run_flag() == 1) {
        const size_t buf_size = 2048;
        char *buf = calloc(buf_size, sizeof(char));
        ssize_t this_round_bytes_read = read(diskfd, (void*)buf, buf_size);
        if (this_round_bytes_read <= -1)
        {
            log_error("Could not read back from file");
            exit(14);
        }
        else if (0 == this_round_bytes_read)
        {
            printf("Finished reading from file.\n");

            free(buf);
            break;
        }

        free(buf);
    }
#endif

    if (-1 == lseek(diskfd, 0, SEEK_END))
    {
        log_error("Could not reset file position to end");
        exit(15);
    }
}

#if 0
// Each time this is called, it will return the first packet in its internally-managed
// buffer. It returns NULL when there is no packet available. (Read: there is no newline
// character)
char* extract_packets(int sockfd, char *new_data, size_t *new_data_len)
{
    static size_t buf_size = 1024;
    static size_t buf_index = 0;
    const size_t size_increment = 1024;
    static char* longterm_buf = calloc(buf_size, sizeof(char));

    // First, check if there is a newline in the new_data.

    
    // Size up to accomodate large messages
    while (buf_size <  new_data_len)
    {
        longterm_buf = realloc(longterm_buf, buf_size + size_increment);
        buf_size += size_increment;
    }

}
#endif

void handle_packet(int sockfd, int diskfd, char* msg, size_t len)
{
    printf("-> Handling packet of size %zu: \"%s\"\n", len, msg);
    write_all(diskfd, msg, len);
    char newline[1] = "\n";
    write_all(diskfd, newline, 1);
    spit_file_back_out_to_socket(sockfd, diskfd);
}

int handle_connection(int sockfd, int diskfd)
{
    ssize_t total_bytes_read = 0;
    ssize_t this_round_bytes_read = 0;
    char *buf = calloc(MAX_BUF_SIZE, sizeof(char));
    //char *leftovers_buf = calloc(MAX_BUF_SIZE, sizeof(char));

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

        size_t bytes_processed = 0;
        char* save_ptr = NULL;

        char* token = NULL;
        char* search_str = buf;
        for(char* token = NULL; ; search_str = NULL)
        {
            token = strtok_r(search_str, "\n", &save_ptr);
            if (NULL == token)
                break;
            else
            {

                printf("=> Token found");
                printf("==> Packet:[");
                print_all(token);
                printf("]\n");
                size_t token_len = strlen(token);
                bytes_processed += token_len;
                handle_packet(sockfd, diskfd, token, token_len);

            }
        }

        free(buf);
        buf = malloc(MAX_BUF_SIZE);
    }
    free(buf);
    //free(leftovers_buf);
    fsync(diskfd);
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

    // Open disk file
    printf("-> Opening disk file.\n");
    int file_mode = S_IWGRP | S_IWUSR | S_IRGRP | S_IRUSR;
    int diskfd = open("/var/tmp/aesdsocketdata", O_CREAT | O_APPEND | O_RDWR, file_mode);
    if (diskfd < 0)
    {
        log_error("Could not open file for writing");
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
        int handle_result = handle_connection(connection_socket_fd, diskfd);
        if (0 != handle_result)
        {
            fprintf(stderr, "Error handling connection from %s\n", client_address.sa_data);
            syslog(LOG_ERR, "Error handling connection from %s", client_address.sa_data);
        }
    }

    printf("-> Closing socket.\n");
    close(sockfd);
    close(diskfd);
    printf("-> Exiting.\n");
}

