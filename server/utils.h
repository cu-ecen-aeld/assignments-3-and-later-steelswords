#ifndef _AESD_ASSIGNMENT_UTILS_H_
#define _AESD_ASSIGNMENT_UTILS_H_

#include <stdint.h>
#include <stdlib.h>

int write_all(int fd, const char* buf, size_t len);

void spit_file_back_out_to_socket(int sockfd, int diskfd);

void log_error(const char *msg);

int duplicate_data_across_fds(int input_fd, int output_fd);

typedef struct _GlobalServerState {
    int diskfd;
    int listen_sockfd;
    struct ThreadList *list_head;
} GlobalServerState;

#endif /* _AESD_ASSIGNMENT_UTILS_H_ */
