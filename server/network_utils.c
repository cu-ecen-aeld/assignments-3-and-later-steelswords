#include <string.h>
#include "utils.h"
#include <stdio.h>
#include "network_utils.h"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <netdb.h>
#include <fcntl.h>
#include <syslog.h>

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


int bind_to_localhost(int sockfd, const char *port)
{
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));

    struct addrinfo *res;

    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    hints.ai_protocol = 0;

    int getaddrinfo_result = getaddrinfo(NULL, port, &hints, &res);
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
int get_bound_socket(const char* port)
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
    printf("-> Binding to socket @ port %s\n", port);
    bind_to_localhost(sockfd, port);

    return sockfd;
}


