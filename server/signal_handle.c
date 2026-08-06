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

// We need at least C11 to use stdatomic.h
#ifndef __STDC_VERSION__
#error "Expected __STDC_VERSION__ macro to be defined; it was not. Incompatible toolchain detected."
#endif
#if __STDC_VERSION__ < 2011L
#error "The server program requires at least the C11 standard. This version is too early. Incompatible toolchain detected."
#endif
#include <stdatomic.h>


/** Flag cleared by sigint and sigterm handler. When it clears, we don't do another
 * loop of listen(), accept(), etc: We clean up and terminate. */
atomic_bool *_run_flag;

/** This flag indicates we are in a blocked state waiting for listen() or accept()
 * to complete. */
atomic_bool *_is_listening_flag;

int init_run_flag()
{
    _run_flag = malloc(sizeof(atomic_bool));
    _is_listening_flag = malloc(sizeof(atomic_bool));
    atomic_init(_run_flag, true);
    atomic_store(_run_flag, true);
    atomic_init(_is_listening_flag, false);
    return 0;
}

int get_run_flag()
{
    return atomic_load(_run_flag);
}


int set_run_flag(bool flag_value)
{
    atomic_store(_run_flag, flag_value);
    return 0;
}

void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM)
    {
        syslog(LOG_WARNING, "Caught signal, exiting");
        printf("-> Caught signal, exiting.\n");

        if (atomic_load(_is_listening_flag)) {
            printf("-> Removing /var/tmp/aesdsocketdata\n");
            if (0 != remove("/var/tmp/aesdsocketdata"))
            {
                fprintf(stderr, "Could not remove /var/tmp/aesdsocketdata\n");
            }
            _exit(EXIT_SUCCESS);
            kill(getpid(), SIGKILL);
        }

        // Mask all other signals so we can clean up safely
        // TODO


        set_run_flag(false);
        printf("-> Returning from signal handler\n");
    }
}

void set_up_signals()
{
    if (SIG_ERR == signal(SIGINT, signal_handler))
        syslog(LOG_ERR, "Could not set up handler for SIGINT");
    if (SIG_ERR == signal(SIGTERM, signal_handler))
        syslog(LOG_ERR, "Could not set up handler for SIGINT");
}
