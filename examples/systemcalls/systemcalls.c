#include "systemcalls.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>
#include <syslog.h>

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>

/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 *   successfully using the system() call, false if an error occurred,
 *   either in invocation of the system() call, or if a non-zero return
 *   value was returned by the command issued in @param cmd.
*/
bool do_system(const char *cmd)
{

/*
 * TODO  add your code here
 *  Call the system() function with the command set in the cmd
 *   and return a boolean true if the system() call completed with success
 *   or false() if it returned a failure
*/
    syslog(LOG_DEBUG, "!!!!!!!! system(\"%s\") called.\n", cmd);

    if (system(cmd))
    {
        syslog(LOG_ERR, "Could not call system(\"%s\"): %s",
                cmd, strerror(errno));

        return false;
    }
    else {
        syslog(LOG_INFO, "system() call succeeded\n");
        return true;
    }
}
/**
* @param count -The numbers of variables passed to the function. The variables are command to execute.
*   followed by arguments to pass to the command
*   Since exec() does not perform path expansion, the command to execute needs
*   to be an absolute path.
* @param ... - A list of 1 or more arguments after the @param count argument.
*   The first is always the full path to the command to execute with execv()
*   The remaining arguments are a list of arguments to pass to the command in execv()
* @return true if the command @param ... with arguments @param arguments were executed successfully
*   using the execv() call, false if an error occurred, either in invocation of the
*   fork, waitpid, or execv() command, or if a non-zero return value was returned
*   by the command issued in @param arguments with the specified arguments.
*/


/**
 * @brief Awaits a child process and waits til it exits. When it does, returns
 * the return code of that child process.
 * @param pid The PID of the child process to await
 * @return The exit code of the child process.
 */
int await_child_proc(pid_t pid)
{
    int wstatus = 0;
    while (true)
    {
        pid_t child_await_event_pid = 0;
        if (-1 == (child_await_event_pid = waitpid(-1, &wstatus, 0)))
        {
            syslog(LOG_ERR, "Error awaiting child process: %s", strerror(errno));
            perror("Error awaiting child process");
        }

        if (child_await_event_pid != pid)
        {
            continue;
        }

        if (WIFEXITED(wstatus))
        {
            int return_code = WEXITSTATUS(wstatus);
            syslog(LOG_INFO, "-> Child process exited with code %d\n", return_code);
            return return_code;
        }
    }
}

bool do_fork_exec(const char* command, char* args[], int *child_pid)
{
    *child_pid = fork();
    if (*child_pid == 0)
    {
        // This is the child process. Exec.
        execv(command, args);
        syslog(LOG_ERR, "%s:%d: Something went wrong exec-ing: %s", __func__,
                __LINE__, strerror(errno));
        exit(EXIT_FAILURE);
    }
    else if (*child_pid == -1) {
        syslog(LOG_ERR, "%s:%d: Something went wrong forking: %s", __func__, __LINE__,
                strerror(errno));
        return false;
    }
    else {
        // This is the parent process. Await the child until it exits.
        int exit_code = await_child_proc(*child_pid);
        syslog(LOG_DEBUG, "%s: Got return code of %d", __func__, exit_code);
        return (exit_code == 0);
    }
}

bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;

    syslog(LOG_DEBUG, "$$$$$$$$$ do_exec called with following arguments:\n");

    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
        syslog(LOG_DEBUG, "- \"%s\"\n", command[i]);
    }
    command[count] = NULL;
    // this line is to avoid a compile warning before your implementation is complete
    // and may be removed

/*
 *   Execute a system command by calling fork, execv(),
 *   and wait instead of system (see LSP page 161).
 *   Use the command[0] as the full path to the command to execute
 *   (first argument to execv), and use the remaining arguments
 *   as second argument to the execv() command.
 *
*/

    int child_pid = 0;
    bool did_succeed = do_fork_exec(command[0], command, &child_pid);
    va_end(args);
    syslog(LOG_INFO, "%s: Succeeded? %c", __func__, (did_succeed)? 'Y' : 'N');
    syslog(LOG_INFO, "%s: Returning %d", __func__, did_succeed);
    return did_succeed;
}

/**
* @param outputfile - The full path to the file to write with command output.
*   This file will be closed at completion of the function call.
* All other parameters, see do_exec above
*/
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    va_list args;
    va_start(args, count);
    char * command[count+1];
    int i;
    bool did_succeed = false;

    syslog(LOG_DEBUG, "$$$$$$$$$ do_exec_redirect() called with following arguments:\n");
    for(i=0; i<count; i++)
    {
        command[i] = va_arg(args, char *);
        syslog(LOG_DEBUG, "- \"%s\"\n", command[i]);
    }
    command[count] = NULL;

    // Create file to redirect to.
    int redirect_fd = -1;
    if (0 > (redirect_fd = open(outputfile, O_WRONLY | O_TRUNC | O_CREAT, 0666)))
    {
        syslog(LOG_ERR, "Could not open file '%s' for process redirection: %s",
                outputfile,
                strerror(errno));
        did_succeed = false;
        goto cleanup;
    }
    if (dup2(redirect_fd, 1) < 0)
    {
        syslog(LOG_ERR, "Could not dup2 STDOUT: %s", strerror(errno));
        close(redirect_fd);
        did_succeed = false;
        goto cleanup;
    }

    if (dup2(redirect_fd, 2) < 0)
    {
        syslog(LOG_ERR, "Could not dup2 STDERR: %s", strerror(errno));
        close(redirect_fd);
        did_succeed = false;
        goto cleanup;
    }
    close(redirect_fd);

    pid_t child_pid = (int)0;
    did_succeed = do_fork_exec(command[0], command, &child_pid);
cleanup:
    va_end(args);

    syslog(LOG_DEBUG, "execv() success? %c", (did_succeed) ? 'Y' : 'N');
    return did_succeed;
}
