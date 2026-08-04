#include "signal_handle.h"
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>

static sem_t* run_flag_semaphore;
static const unsigned int RUN_FLAG_TRUE = 1;
static const unsigned int RUN_FLAG_FALSE = 0;


int init_run_flag()
{
    run_flag_semaphore = sem_open("run_flag_semaphore", O_CREAT | O_EXCL);
    if (SEM_FAILED == run_flag_semaphore)
    {
        perror("Could not open semaphore for run flag");
        exit(EXIT_FAILURE);
    }
}

int get_run_flag()
{
    return -1;
}


int set_run_flag(bool flag_value)
{
    return -1;
}
