#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <unistd.h>
#include <semaphore.h>
#include <errno.h>
#include <stdlib.h>

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
    fprintf(stderr, "%s: %s", msg, errno_reason);
    syslog(LOG_ERR, "%s: %s", msg, errno_reason);
}

int main(int argc, char** argv)
{
    // Open socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        log_error("Could not open socket");
        exit(EXIT_FAILURE);
    }

    // Bind to socket
    const char * const localhost_address = "0.0.0.0:9000";
    struct sockaddr sa = {
        .sa_family = AF_INET,
        .sa_data = {0},
    };
    strncpy(sa.sa_data, localhost_address, strlen(localhost_address));
    int bind_result = bind(sockfd, &sa, sizeof(struct sockaddr));
    if (bind_result < 0)
    {
        log_error("Could not bind to socket");
        exit(EXIT_FAILURE);
    }

    close(sockfd);
}

