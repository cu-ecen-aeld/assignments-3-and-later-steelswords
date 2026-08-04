#ifndef AESD_SIGNAL_HANDLE_H_
#define AESD_SIGNAL_HANDLE_H_

#include <stdbool.h>

/** Initializes the run flag as true. */
int init_run_flag();

/**
 * Returns 1 if program should still run.
 * Returns 0 if program should terminate.
 * Returns -1 and sets errno if error occurred.
 */
int get_run_flag();

/** Sets the run flag.
 * See also: get_run_flag()
 */
int set_run_flag(bool flag_value);

#endif /* AESD_SIGNAL_HANDLE_H_ */
