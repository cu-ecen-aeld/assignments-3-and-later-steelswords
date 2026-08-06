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
#include <stdbool.h>
#include <arpa/inet.h>
#include <stdatomic.h>
#include "signal_handle.h"

extern atomic_bool *_run_flag;
extern atomic_bool *_is_listening_flag;

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

    struct addrinfo *res;

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
    freeaddrinfo(res);
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

    if (-1 == lseek(diskfd, 0, SEEK_END))
    {
        log_error("Could not reset file position to end");
        exit(15);
    }
}

/** Like strtok, but it manually rearranges the buffer when a token is found, and scootches
 * the remaining contents to the beginning of the buffer. Can be called many times on the same  */
char* extract_token_and_consolidate_buffer(char *buf, size_t len)
{
    printf("-> Checking for newlines in recevied messages.\n");
    for (size_t i = 0; i < len; ++i)
    {
        //printf("%02x ", buf[i]);
        //if (i % 16 == 0) printf("\n");
        if (buf[i] == '\n')
        {
            printf("--> Found newline @ i = %zu (len = %zu)\n", i, len);
            // That's a token!
            // Chop it off there.
            buf[i] = '\0';
            // Copy it to a malloced string.
            char *result = calloc(i + 1, sizeof(char));
            if (result == NULL)
            {
                log_error("Cannot allocate more memory");
                return NULL;
            }
            strncpy(result, buf, i+1);

            // Scootch the beginning of unused data to the front of the buffer.
            i++; // Skip the null terminator of the last string.
            for (size_t j = 0; i < len; ++j, ++i)
            {
                buf[j] = buf[i];
            }

            // Now consolidate buf.
            return result;
        }
    }
    return NULL;
}

void handle_packet(int sockfd, int diskfd, char* msg, size_t len)
{
    //printf("-> Handling packet of size %zu: \"%s\"\n", len, msg);
    printf("-> Handling packet of size %zu\n", len);
    write_all(diskfd, msg, len);
    char newline[1] = "\n";
    write_all(diskfd, newline, 1);
    spit_file_back_out_to_socket(sockfd, diskfd);
}

char* read_until_stop_condition(int sockfd, int diskfd, size_t *len)
{
    const size_t buf_chunk_size = 1024;
    ssize_t bytes_read = 0;
    ssize_t n = 0; // Bytes read this go-around.
    bool keep_receiving = true;
    size_t index = 0;
    *len = buf_chunk_size;
    char* buf = calloc(*len, sizeof(char));
    if (NULL == buf)
    {
        log_error("Cannot allocate mem for buffer");
        exit(-1);
    }

    while (true)
    {
        n = recv(sockfd, &buf[index], buf_chunk_size, 0);
        printf(" * Recved %zu bytes\n", n);
        if (n == buf_chunk_size)
        {
            index += n;
            // Resize.
            buf = realloc(buf, *len + buf_chunk_size);
            *len += buf_chunk_size;
            printf("-> Resized buffer to be %zu\n", *len);
            if (!buf)
            {
                log_error("Cannot resize buffer");
                exit(-1);
            }
            // Set the new bytes to 0.
            memset(&buf[index], 0, buf_chunk_size);
        }
        else if (n == -1)
        {
            log_error("Could not recv from socket");
            return NULL;
        }
        else if (n == 0)
        {
            // Socket performed an orderly shutdown.
            printf("-> Socket shut down in an orderly way.\n");
            return buf;
        }
        else
        {
            if (n + index >= *len)
            {
                index += n;
                // Resize.
                buf = realloc(buf, *len + buf_chunk_size);
                *len += buf_chunk_size;
                printf("-> Resized buffer to be %zu\n", *len);
                if (!buf)
                {
                    log_error("Cannot resize buffer");
                    exit(-1);
                }
                // Set the new bytes to 0.
                memset(&buf[index], 0, buf_chunk_size);

            }
            char *packet = NULL;
            while (NULL != (packet = extract_token_and_consolidate_buffer(buf, *len))) 
            {
                //printf("-> Handling packet: [%s]\n", packet);
                handle_packet(sockfd, diskfd, packet, strlen(packet));
                free(packet);

                // TODO: The requirements here are murky. Might have to remove this.
                keep_receiving = false;
            }
            return buf;
        }
    }
    if (buf)
        free(buf);
}

int handle_connection(int sockfd, int diskfd)
{
    ssize_t this_round_bytes_read = 0;
    size_t buf_size = MAX_BUF_SIZE;
    size_t index = 0; // This marks where recv is supposed to put data. It is also the total bytes read.
    bool received_full_token = false;

    char* full_msg = read_until_stop_condition(sockfd, diskfd, &buf_size);
    extract_token_and_consolidate_buffer(full_msg, buf_size);
    free(full_msg);


    return 0;
}

//void get_client_ip_address(int client_sockfd, struct sockaddr *client_addr, char* out_result)
// Based on example code from Beej's Guide
// out_result is allocated on the caller's side. It must be at least INET6_ADDRSTRLEN chars long.
void get_client_ip_address(int client_sockfd, char* out_result)
{
    strcpy(out_result, "unknown");
    // First, get the sockaddr info about the client peer.
    socklen_t len = sizeof (struct sockaddr_storage);
    struct sockaddr_storage addr;
    int port;
    getpeername(client_sockfd, (struct sockaddr*)&addr, &len);

    if (AF_INET == addr.ss_family)
    {
        struct sockaddr_in *sockinfo = (struct sockaddr_in *)&addr;
        port = ntohs(sockinfo->sin_port);
        inet_ntop(AF_INET, &sockinfo->sin_addr, out_result, INET6_ADDRSTRLEN);
    } else { // IPv6
        struct sockaddr_in6 *sockinfo = (struct sockaddr_in6 *)&addr;
        port = ntohs(sockinfo->sin6_port);
        inet_ntop(AF_INET6, &sockinfo->sin6_addr, out_result, INET6_ADDRSTRLEN);
    }
}



int main(int argc, char** argv)
{
    bool do_daemon_mode = false;

    openlog("aesdsocket", LOG_CONS, LOG_USER);

    if (argc == 2)
    {
        if (0 == strcmp(argv[1], "-d"))
        {
            printf(" * -d flag passed in.\n");
            do_daemon_mode = true;
        }
    }

    printf("-> Setting up run flag\n");
    int res = init_run_flag();

    printf("-> Setting up signal handler");
    set_up_signals();

    printf("-> Run flag = %d\n", get_run_flag());
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


    if (do_daemon_mode)
    {
        if (0 != daemon(0, 1))
        {
            log_error("Could not daemonize server!\n");
            exit(errno);
        }
        else
        {
            printf("->> DAEMON MODE ACTIVATED\n");
        }
    }

    while (true == get_run_flag())
    {
        printf("-> Listening for connections on socket.\n");
        atomic_store(_is_listening_flag, true);
        int listen_res = listen(sockfd, SOMAXCONN);
        if (-1 == listen_res)
        {
            log_error("Could not listen to socket");
            exit(3);
        }

        struct sockaddr client_address;
        socklen_t client_address_length = 0;
        memset(&client_address, 0, sizeof(struct sockaddr));

        // Accept the connection
        int connection_socket_fd = accept(sockfd, &client_address, &client_address_length);
        atomic_store(_is_listening_flag, false);

        char client_ip_address[INET6_ADDRSTRLEN] = {0};
        get_client_ip_address(connection_socket_fd, client_ip_address);

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

        if (0 == close(connection_socket_fd))
        {
            syslog(LOG_INFO, "Closed connection from %s", client_ip_address);
            printf(" * Closed connection from %s\n", client_ip_address);
        }
        else
        {
            log_error("Could not close connection from client");
        }
    }

    printf("-> Closing socket.\n");
    close(sockfd);
    close(diskfd);
    free(_run_flag);
    free(_is_listening_flag);

    if (0 != remove("/var/tmp/aesdsocketdata"))
    {
        log_error("Could not remove /var/tmp/aesdsocketdata");
    }

    printf("-> Exiting.\n");
}
