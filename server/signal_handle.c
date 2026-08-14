#include "signal_handle.h"
#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

// We need at least C11 to use stdatomic.h
#ifndef __STDC_VERSION__
#error "Expected __STDC_VERSION__ macro to be defined; it was not. Incompatible toolchain detected."
#endif
#if __STDC_VERSION__ < 2011L
#error "The server program requires at least the C11 standard. This version is too early. Incompatible toolchain detected."
#endif
#include <stdatomic.h>

// Forward declare what's in server.c
extern void shutdown_operations();

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


static void _sigset_die_if_error(int result, const char* const which)
{
    if (result == 0) return;
    syslog(LOG_ERR, "!! Could not make sigset %s: %s", which, strerror(errno));
    exit(EXIT_FAILURE);
}

sigset_t get_signals_to_mask()
{
    sigset_t mask;
    int res = sigemptyset(&mask);
    _sigset_die_if_error(res, "empty");

    res = sigaddset(&mask, SIGINT);
    _sigset_die_if_error(res, "sigint");

    res = sigaddset(&mask, SIGTERM);
    _sigset_die_if_error(res, "sigterm");

    return mask;
}

void disable_signal_handlers()
{
    sigset_t mask = get_signals_to_mask();

    if (0 != sigprocmask(SIG_BLOCK, &mask, NULL))
    {
        syslog(LOG_ERR, "Could not mask sigint and sigterm: %s",
                strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void reenable_signal_handlers()
{
    sigset_t mask = get_signals_to_mask();

    if (0 != sigprocmask(SIG_UNBLOCK, &mask, NULL))
    {
        syslog(LOG_ERR, "Could not mask sigint and sigterm: %s",
                strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM)
    {
        syslog(LOG_WARNING, "Caught signal, exiting");
        printf("-> Caught signal, exiting.\n");
        set_run_flag(false);
        //disable_signal_handlers();
        //shutdown_operations();
        //reenable_signal_handlers();
    }

#if 0
        if (atomic_load(_is_listening_flag)) {
            printf("-> Removing /var/tmp/aesdsocketdata\n");
            if (0 != remove("/var/tmp/aesdsocketdata"))
            {
                fprintf(stderr, "Could not remove /var/tmp/aesdsocketdata\n");
            }
            free(_run_flag);
            free(_is_listening_flag);
            _exit(EXIT_SUCCESS);
            kill(getpid(), SIGKILL);
        }

        // Mask all other signals so we can clean up safely
        // TODO


        set_run_flag(false);
        printf("-> Returning from signal handler\n");
    }
#endif
}

void set_up_signals()
{

#if 0
    struct sigaction action = {
        .sa_handler = signal_handler,

    };
#endif

    if (SIG_ERR == signal(SIGINT, signal_handler))
        syslog(LOG_ERR, "Could not set up handler for SIGINT");
    if (SIG_ERR == signal(SIGTERM, signal_handler))
        syslog(LOG_ERR, "Could not set up handler for SIGINT");
}
