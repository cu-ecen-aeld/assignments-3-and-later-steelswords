#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

void set_up_logging()
{
    openlog("writer", LOG_PERROR, LOG_USER);
}

void exit_logfully(int exit_code)
{
    syslog(LOG_INFO, "Terminating writer program");
    closelog();
    exit(exit_code);
}

int main(int argc, char** argv)
{
    set_up_logging();
    if (argc != 3)
    {
        syslog(LOG_ERR, "Usage: %s <target_file> <content_string>\n", argv[0]);
        exit_logfully(1);
    }

    const char* target_file_name = argv[1];
    const char* content_string = argv[2];
    int fd = open(target_file_name, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd == -1)
    {
        syslog(LOG_ERR, "Could not open file for writing: %s", strerror(errno));
        exit_logfully(1);
    }

    ssize_t bytes_written = 0;
    const size_t MAX_STRING_SIZE = 1024 * 1024 * 100;
    ssize_t content_size = strnlen(content_string, MAX_STRING_SIZE);
    if (content_size == MAX_STRING_SIZE)
    {
        syslog(LOG_ERR, "Max string size reached. Are you trying to buffer overflow me right now? Rude.");
    }

    syslog(LOG_DEBUG, "Writing %s to file %s", content_string, target_file_name);
#if 0
    syslog(LOG_INFO, "Writing %zi bytes of content ('%s') to file %s",
            content_size,
            content_string,
            target_file_name);
#endif

    while (bytes_written < content_size)
    {
        ssize_t bytes_written_this_cycle = write(fd, content_string, content_size);
        if (bytes_written_this_cycle == -1)
        {
            syslog(LOG_ERR, "Failed to write to %s: %s",
                    target_file_name,
                    strerror(errno));
            exit_logfully(1);
        }
        else
        {
            bytes_written += bytes_written_this_cycle;
            content_string += bytes_written_this_cycle;
        }
    }

    exit_logfully(0);
}
