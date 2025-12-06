#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
 
int main(int argc, char *argv[])
{
    openlog("writer", LOG_PID, LOG_USER);
 
    if (argc != 3) {
        syslog(LOG_ERR, "Invalid number of arguments: %d", argc - 1);
        fprintf(stderr, "Usage: %s <writefile> <writestr>\n", argv[0]);
        closelog();
        return 1;
    }
 
    const char *writefile = argv[1];
    const char *writestr  = argv[2];
 

    syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
 

    int fd = open(writefile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "Error opening file %s: %s", writefile, strerror(errno));
        perror("open");
        closelog();
        return 1;
    }
 
    size_t len = strlen(writestr);
    ssize_t written = write(fd, writestr, len);
 
    if (written == -1 || (size_t)written != len) {
        syslog(LOG_ERR, "Error writing to file %s: %s", writefile, strerror(errno));
        perror("write");
        close(fd);
        closelog();
        return 1;
    }
 
    if (close(fd) == -1) {
        syslog(LOG_ERR, "Error closing file %s: %s", writefile, strerror(errno));
        perror("close");
        closelog();
        return 1;
    }
 
    closelog();
    return 0;
}
