#include "signal_handle.h"
#include <semaphore.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <errno.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <signal.h>
#include <syslog.h>

int foo()
{
}
