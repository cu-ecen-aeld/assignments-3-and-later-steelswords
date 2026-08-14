/** aesdsocket program for assignment 6
 * file: server.c
 * author: Tristan Andrus (steelswords)
 */
#define _GNU_SOURCE // Needed by gettid
#include "signal_handle.h"
#include "network_utils.h"
#include "utils.h"
#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/queue.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

struct ThreadListNode {
    pthread_t thread_handle;
    // TODO: Remove
    bool still_running;
    bool still_listening;
    SLIST_ENTRY(ThreadListNode) nodes;
};
SLIST_HEAD(ThreadList, ThreadListNode);

/** @section Global Variables { */
struct ThreadList *g_thread_list_head = NULL;

extern atomic_bool *_run_flag;
extern atomic_bool *_is_listening_flag;
extern atomic_bool *_timestamp_due_flag;

pthread_mutex_t g_file_mutex = PTHREAD_MUTEX_INITIALIZER;

#define MAX_BUF_SIZE (4 * 1024)

/** @section } */

// Forward declarations
void get_client_ip_address(int client_sockfd, char* out_result);
void* handle_connection(void*);

#define MAX_TIMESTAMP_LEN (998)
char* get_timestamp_str()
{
    //struct timespec tp;
    //if (0 != clock_gettime(CLOCK_REALTIME, &tp))
    //{
    //    log_error("could not get accurate timestamp.");
    //}
    time_t now = time(NULL);
    struct tm *human_time = localtime(&now);

    char* result = calloc(MAX_TIMESTAMP_LEN + 1, sizeof(char));
    strftime(result, MAX_TIMESTAMP_LEN, "%a, %d %b %Y %T %z", human_time);
    return result;
}

void *print_timestamp_on_time(void* data)
{
    GlobalServerState* state = (GlobalServerState*)data;
    int diskfd = state->diskfd;
    while(true == get_run_flag())
    {
        // Wait until flag is true
        while ((true == get_run_flag()) && (false == atomic_load(_timestamp_due_flag)))
        {
            usleep(100* 1000);
        }
        pthread_mutex_lock(&g_file_mutex);
        char *timestamp_str = get_timestamp_str();
        dprintf(diskfd, "timestamp:%s\n", timestamp_str);
        pthread_mutex_unlock(&g_file_mutex);
        atomic_store(_timestamp_due_flag, false);
        free(timestamp_str);
    }
    return NULL;
}

void add_thread_to_list(struct ThreadList* head, pthread_t tid)
{
    struct ThreadListNode* node = malloc(sizeof(struct ThreadListNode));
    node->thread_handle = tid;
    node->still_running = true;
    node->still_listening = false;
    SLIST_INSERT_HEAD(head, node, nodes);
}

/** Listens as long as the run flag is operational. When clients connect, this
 * dispatches connection handler threads. */
void *listen_loop(void *global_server_state)
{
    GlobalServerState *state = (GlobalServerState *)global_server_state;
    int diskfd = state->diskfd;
    int sockfd = state->listen_sockfd;
    int connection_number = 0;

    while (true == get_run_flag())
    {
        printf("-> Listening for connections on socket.\n");
        while (true == get_run_flag())
        {
            if (-1 == listen(sockfd, SOMAXCONN))
            {
                if (errno == EAGAIN)
                {
                    usleep(50*1000);
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
            //printf(" * Trying to accept connection.\n");
            connection_socket_fd = accept(sockfd, &client_address, &client_address_length);
            if (-1 == connection_socket_fd)
            {
                if (errno == EAGAIN)
                {
                    usleep(20*1000);
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
        if (false == get_run_flag())
        {
            printf("-> Exiting listen loop.\n");
            close(sockfd);
            return NULL;
        }


        pthread_t new_thread_handle;
#if 0
        GlobalServerState state = {
            .diskfd = diskfd,
            .listen_sockfd = connection_socket_fd,
            .list_head = g_thread_list_head,
        };
#endif
        GlobalServerState *state = malloc(sizeof(GlobalServerState));
        if (NULL == state)
        {
            log_error("Could not allocate more memory");
            exit(10);
        }
        state->diskfd = diskfd;
        state->listen_sockfd = connection_socket_fd;
        state->list_head = g_thread_list_head;

        char thread_name[16] = {0};
        sprintf(thread_name, "CONCTN%03d-%03d", connection_socket_fd, connection_number);
        printf(" * Starting thread %s\n", thread_name);

        if (0 != pthread_create(&new_thread_handle, NULL, handle_connection, (void*)state))
        {
            log_error("Could not create thread to handle new connection.");
            exit(6);
        }

        pthread_setname_np(new_thread_handle, thread_name);
        add_thread_to_list(g_thread_list_head, new_thread_handle);
        connection_number++;
    }
    close(sockfd);
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

void* handle_connection(void *args)
{
    GlobalServerState *state = (GlobalServerState*)args;
    int sockfd = state->listen_sockfd;
    int diskfd = state->diskfd;
    free(args);

    char client_ip_address[INET6_ADDRSTRLEN] = {0};
    get_client_ip_address(sockfd, client_ip_address);

    syslog(LOG_INFO, "Accepted connection from %s", client_ip_address);
    printf(" * Accepted connection from %s\n", client_ip_address);

    size_t buf_size = MAX_BUF_SIZE;

    char* full_msg = read_until_stop_condition(sockfd, diskfd, &buf_size);
    extract_token_and_consolidate_buffer(full_msg, buf_size);
    free(full_msg);

    if (0 == close(sockfd))
    {
        syslog(LOG_INFO, "Closed connection from %s", client_ip_address);
        printf(" * Closed connection from %s\n", client_ip_address);
    }
    else
    {
        log_error("Could not close connection from client");
    }

    return NULL;
}


void shutdown_operations()
{
    printf("-> Shutting down.\n");
    set_run_flag(false);

    printf("-> Joining all threads.\n");
    // Join each thread
    struct ThreadListNode *node = NULL;
    node = SLIST_FIRST(g_thread_list_head);
    while (node != NULL)
    {
        pthread_join(node->thread_handle, NULL);
        SLIST_REMOVE(g_thread_list_head, node, ThreadListNode, nodes);

        struct ThreadListNode *tmp = node;
        node = SLIST_NEXT(node, nodes);
        free(tmp);
    }
    free(g_thread_list_head);
}

/** Sets up an interval timer for every `every_secs` seconds. And starts the thread to watch
 * for changes and run the timestamp code. The caller is responsible for freeing `state`. */
pthread_t set_up_timestamp_timer(time_t every_secs, GlobalServerState *state)
{
    struct timeval interval = {
        .tv_sec = every_secs,
        .tv_usec = 0,
    };
    struct itimerval timer_interval = {
        .it_interval = interval,
        .it_value = interval,
    };

    setitimer(ITIMER_REAL, &timer_interval, NULL);

    pthread_t tid;
    if (0 != pthread_create(&tid, NULL, print_timestamp_on_time, state))
    {
        log_error("!! Could not create timestamp thread");
    }
    pthread_setname_np(tid, "timestamppnt");
    return tid;
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

    printf("-> Run flag = %d\n", get_run_flag());
    if (res != 0)
    {
        log_error("Could not set up run flag.");
        exit(EXIT_FAILURE);
    }

    printf("-> Setting up signal handler");
    set_up_signals();

    // Open disk file
    printf("-> Opening disk file.\n");
    int file_mode = S_IWGRP | S_IWUSR | S_IRGRP | S_IRUSR;
    int diskfd = open("/var/tmp/aesdsocketdata", O_CREAT | O_APPEND | O_RDWR, file_mode);
    if (diskfd < 0)
    {
        log_error("Could not open file for writing");
        exit(EXIT_FAILURE);
    }

   int sockfd = get_bound_socket("9000"); 

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

    pthread_t timestamp_thread_handle = set_up_timestamp_timer(10, &listen_loop_args);

    pthread_t listen_loop_handle = {0};
    if (0 != pthread_create(&listen_loop_handle, NULL, &listen_loop, (void*)&listen_loop_args))
    {
        syslog(LOG_ERR, "Could not create listen loop thread: %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    pthread_setname_np(listen_loop_handle, "listen_loop");

    // This is the reaper. Wait for run flag to be cleared.
    while (get_run_flag() == true)
    {
        usleep(200 * 1000);
    }

    printf("-> Proceeding with shutdown.\n");

    pthread_join(listen_loop_handle, NULL);
    pthread_join(timestamp_thread_handle, NULL);

    shutdown_operations();

    printf("-> Closing socket.\n");
    close(sockfd);
    close(diskfd);

    free(_run_flag);
    free(_is_listening_flag);
    free(_timestamp_due_flag);

    if (0 != remove("/var/tmp/aesdsocketdata"))
    {
        log_error("Could not remove /var/tmp/aesdsocketdata");
    }

    printf("-> Exiting.\n");
}
