#include "systemcalls.h"
 
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
 
/**
 * @param cmd the command to execute with system()
 * @return true if the command in @param cmd was executed
 * successfully using the system() call, false if an error occurred,
 * either in invocation of the system() call, or if a non-zero return
 * value was returned by the command issued in @param cmd.
 */
bool do_system(const char *cmd)
{
    if (cmd == NULL) {
        return false;
    }
 
    int status = system(cmd);
 
    if (status == -1) {
        // system() itself failed
        return false;
    }
 
    // Consider it success only if the command exited normally with code 0
    if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)) {
        return true;
    }
 
    return false;
}
 
/**
 * @param count -The numbers of variables passed to the function. The variables are command to execute.
 * followed by arguments to pass to the command
 * Since exec() does not perform path expansion, the command to execute needs
 * to be an absolute path.
 *
 * @param ... - A list of 1 or more arguments after the @param count argument.
 * The first is always the full path to the command to execute with execv()
 * The remaining arguments are a list of arguments to pass to the command in execv()
 *
 * @return true if the command @param ... with arguments @param arguments were
 * executed successfully using the execv() call, false if an error occurred,
 * either in invocation of the fork, waitpid, or execv() command, or if a
 * non-zero return value was returned by the command issued in @param arguments
 * with the specified arguments.
 */
bool do_exec(int count, ...)
{
    va_list args;
    va_start(args, count);
 
    // +1 for NULL terminator required by execv
    char *command[count + 1];
 
    for (int i = 0; i < count; i++) {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
 
    va_end(args);
 
    pid_t pid = fork();
    if (pid == -1) {
        // fork failed
        return false;
    }
 
    if (pid == 0) {
        // Child: replace image with requested program
        execv(command[0], command);
 
        // If execv returns, it failed
        _exit(EXIT_FAILURE);
    }
 
    // Parent: wait for child
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        return false;
    }
 
    if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)) {
        return true;
    }
 
    return false;
}
 
/**
 * Like do_exec(), but redirects stdout of the executed program
 * to the file specified in @param outputfile.
 *
 * @param outputfile - path to the file to which stdout should be redirected
 * @param count, ... - same semantics as do_exec()
 *
 * @return true on successful execution (child exits with status 0),
 * false on any error (open, fork, dup2, waitpid, or non-zero exit).
 */
bool do_exec_redirect(const char *outputfile, int count, ...)
{
    if (outputfile == NULL) {
        return false;
    }
 
    va_list args;
    va_start(args, count);
 
    char *command[count + 1];
 
    for (int i = 0; i < count; i++) {
        command[i] = va_arg(args, char *);
    }
    command[count] = NULL;
 
    va_end(args);
 
    // Open (or create) the output file
    int fd = open(outputfile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        return false;
    }
 
    pid_t pid = fork();
    if (pid == -1) {
        close(fd);
        return false;
    }
 
    if (pid == 0) {
        // Child: redirect stdout to the file
        if (dup2(fd, STDOUT_FILENO) < 0) {
            _exit(EXIT_FAILURE);
        }
        close(fd);
 
        // Execute the requested program
        execv(command[0], command);
 
        // If execv returns, it failed
        _exit(EXIT_FAILURE);
    }
 
    // Parent: no longer need this fd
    close(fd);
 
    int status;
    if (waitpid(pid, &status, 0) == -1) {
        return false;
    }
 
    if (WIFEXITED(status) && (WEXITSTATUS(status) == 0)) {
        return true;
    }
 
    return false;
}
