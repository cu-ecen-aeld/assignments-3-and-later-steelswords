#include "utils.h"
#include <errno.h>
#include <stdlib.h>
#include <syslog.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifndef MAX_BUF_SIZE
#define MAX_BUF_SIZE (1024)
#endif

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

void log_error(const char *msg)
{
    char *errno_reason = strerror(errno);
    fprintf(stderr, "%s: %s\n", msg, errno_reason);
    syslog(LOG_ERR, "%s: %s", msg, errno_reason);
}
