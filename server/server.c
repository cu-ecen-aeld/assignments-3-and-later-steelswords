/** aesdsocket program for assignment 6
 * file: server.c
 * author: Tristan Andrus (steelswords)
 */
#define _GNU_SOURCE // Needed by gettid
#include "utils.h"
#include "signal_handle.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>
#include <sys/queue.h>

struct ThreadListNode {
    pthread_t thread_handle;
    bool still_running;
    bool still_listening;
    SLIST_ENTRY(ThreadListNode) nodes;
};
SLIST_HEAD(ThreadList, ThreadListNode);

/** @section Global Variables { */
struct ThreadList *g_thread_list_head = NULL;

extern atomic_bool *_run_flag;
extern atomic_bool *_is_listening_flag;

pthread_mutex_t g_file_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_BUF_SIZE (4 * 1024)

/** @section } */

// Foreard declarations
void get_client_ip_address(int client_sockfd, char* out_result);
int handle_connection(int sockfd, int diskfd);

void add_thread_to_list(struct ThreadList* head, pthread_t tid)
{
    struct ThreadListNode* node = malloc(sizeof(struct ThreadListNode));
    node->thread_handle = tid;
    node->still_running = true;
    node->still_listening = false;
    SLIST_INSERT_HEAD(head, node, nodes);
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

/** Returns a socket that has had open() and bind() called on it or dies trying.
 * The return value is guaranteed to be a socket file descriptor. */
int get_bound_socket()
{
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
    fcntl(sockfd, F_SETFL, O_NONBLOCK);

    // Bind to socket
    printf("-> Binding to socket\n");
    bind_to_localhost(sockfd);

    return sockfd;
}

typedef struct _GlobalServerState {
    int diskfd;
    int listen_sockfd;
    struct ThreadList *list_head;
} GlobalServerState;

/** Listens as long as the run flag is operational. When clients connect, this
 * dispatches connection handler threads. */
void *listen_loop(void *global_server_state)
{
    GlobalServerState *state = (GlobalServerState *)global_server_state;
    int diskfd = state->diskfd;
    int sockfd = state->listen_sockfd;

    while (true == get_run_flag())
    {
        printf("-> Listening for connections on socket.\n");
        while (true == get_run_flag())
        {
            if (-1 == listen(sockfd, SOMAXCONN))
            {
                if (errno == EAGAIN)
                {
                    //usleep(50*1000);
                    continue;
                }
                log_error("Could not listen to socket");
                exit(3);
            }
            else break;
        }

        struct sockaddr client_address;
        socklen_t client_address_length = 0;
        memset(&client_address, 0, sizeof(struct sockaddr));

        // Accept the connection
        int connection_socket_fd = -1;
        while(true == get_run_flag())
        {
            connection_socket_fd = accept(sockfd, &client_address, &client_address_length);
            if (-1 == connection_socket_fd)
            {
                if (errno == EAGAIN)
                {
                    //usleep(50*1000);
                    continue;
                }
                else
                {
                    log_error("Could not accept incoming connection");
                    exit(4);
                }
            }
            else break;
        }

        char client_ip_address[INET6_ADDRSTRLEN] = {0};
        get_client_ip_address(connection_socket_fd, client_ip_address);

        syslog(LOG_INFO, "Accepted connection from %s", client_ip_address);
        printf(" * Accepted connection from %s\n", client_ip_address);

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
    return NULL;
}

/** Like strtok, but it manually rearranges the buffer when a token is found, and scootches
 * the remaining contents to the beginning of the buffer. Can be called many times on the same  */
char* extract_token_and_consolidate_buffer(char *buf, size_t len)
{
    printf("-> Checking for newlines in received messages.\n");
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
    int this_thread_id = (int)gettid();
    printf("-> Thread %d awaiting write lock.\n", this_thread_id);
    pthread_mutex_lock(&g_file_mutex);
    printf("-> Thread %d: acquired write lock. Writing to file and socket.\n",
            this_thread_id);
    write_all(diskfd, msg, len);
    char newline[1] = "\n";
    write_all(diskfd, newline, 1);
    spit_file_back_out_to_socket(sockfd, diskfd);
    pthread_mutex_unlock(&g_file_mutex);
    printf("-> Thread %d: released write lock.\n", this_thread_id);
}

char* read_until_stop_condition(int sockfd, int diskfd, size_t *len)
{
    const size_t buf_chunk_size = 1024;
    ssize_t n = 0; // Bytes read this go-around.
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
            }
            return buf;
        }
    }
    if (buf)
        free(buf);
}

int handle_connection(int sockfd, int diskfd)
{
    size_t buf_size = MAX_BUF_SIZE;

    char* full_msg = read_until_stop_condition(sockfd, diskfd, &buf_size);
    extract_token_and_consolidate_buffer(full_msg, buf_size);
    free(full_msg);

    return 0;
}

// Based on example code from Beej's Guide
// out_result is allocated on the caller's side. It must be at least INET6_ADDRSTRLEN chars long.
void get_client_ip_address(int client_sockfd, char* out_result)
{
    strcpy(out_result, "unknown");
    // First, get the sockaddr info about the client peer.
    socklen_t len = sizeof (struct sockaddr_storage);
    struct sockaddr_storage addr;
    getpeername(client_sockfd, (struct sockaddr*)&addr, &len);

    if (AF_INET == addr.ss_family)
    {
        struct sockaddr_in *sockinfo = (struct sockaddr_in *)&addr;
        inet_ntop(AF_INET, &sockinfo->sin_addr, out_result, INET6_ADDRSTRLEN);
    } else { // IPv6
        struct sockaddr_in6 *sockinfo = (struct sockaddr_in6 *)&addr;
        inet_ntop(AF_INET6, &sockinfo->sin6_addr, out_result, INET6_ADDRSTRLEN);
    }
}

void shutdown_operations()
{
    printf("-> Shutting down.\n");
    set_run_flag(false);

    printf("-> Joining all threads.\n");
    // Join each thread
    struct ThreadListNode *node = NULL;
    SLIST_FOREACH(node, g_thread_list_head, nodes)
    {
        pthread_join(node->thread_handle, NULL);
    }
    if (0 != remove("/var/tmp/aesdsocketdata"))
    {
        fprintf(stderr, "Could not remove /var/tmp/aesdsocketdata\n");
        syslog(LOG_ERR, "Could not remove /var/tmp/aesdsocketdata");
    }


    free(_run_flag);
    free(_is_listening_flag);
#if 0
    _exit(EXIT_SUCCESS);
    kill(getpid(), SIGKILL);
#endif
}

int spawn_handler_thread(struct ThreadList *list_head, int client_sockfd)
{

    // TODO


    return 0;
}

int main(int argc, char** argv)
{
    g_thread_list_head = malloc(sizeof(struct ThreadListNode));
    SLIST_INIT(g_thread_list_head);

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

   int sockfd = get_bound_socket(); 

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

    GlobalServerState listen_loop_args = {
        .diskfd = diskfd,
        .listen_sockfd = sockfd,
        .list_head = g_thread_list_head,
    };
    pthread_t listen_loop_handle = {0};
    if (0 != pthread_create(&listen_loop_handle, NULL, &listen_loop, (void*)&listen_loop_args))
    {
        syslog(LOG_ERR, "Could not create listen loop thread: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // This is the reaper
    while (get_run_flag() == true)
    {
        usleep(200 * 1000);
    }

    pthread_join(listen_loop_handle, NULL);

    shutdown_operations();

    printf("-> Closing socket.\n");
    close(sockfd);
    close(diskfd);

    if (0 != remove("/var/tmp/aesdsocketdata"))
    {
        log_error("Could not remove /var/tmp/aesdsocketdata");
    }

    printf("-> Exiting.\n");
}
