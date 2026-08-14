#ifndef _AESD_ASSIGNMENT_NETWORK_UTILS_H
#define _AESD_ASSIGNMENT_NETWORK_UTILS_H

/** Based on example code from Beej's Guide
 * out_result is allocated on the caller's side. It must be at least
 * INET6_ADDRSTRLEN chars long. */
void get_client_ip_address(int client_sockfd, char* out_result);

/** Returns a socket that has had open() and bind() called on it or dies trying.
 * The return value is guaranteed to be a socket file descriptor. */
int get_bound_socket(const char* port);

/** Binds a port to 0.0.0.0 at specified `port` */
int bind_to_localhost(int sockfd, const char* port);

#endif /* _AESD_ASSIGNMENT_NETWORK_UTILS_H */
