/**
 * aesdsocket.c
 *
 * Simple TCP server:
 *  - Listens on port 9000
 *  - Accepts connections, logs via syslog
 *  - Receives data until newline, appends to /var/tmp/aesdsocketdata
 *  - After each newline-terminated packet, sends entire file back
 *  - Runs in a loop until SIGINT or SIGTERM
 *  - On exit: logs message, closes socket, removes data file
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <syslog.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#define PORT 9000
#define BACKLOG 10
#define DATA_FILE "/var/tmp/aesdsocketdata"

static volatile sig_atomic_t exit_requested = 0;

/**
 * Signal handler: just set a flag.
 */
static void signal_handler(int signo)
{
    if (signo == SIGINT || signo == SIGTERM) {
        exit_requested = 1;
    }
}

/**
 * Append a buffer to DATA_FILE, creating it if necessary.
 * Returns 0 on success, -1 on error.
 */
static int append_to_file(const char *data, size_t len)
{
    int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "open(\"%s\") failed: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    ssize_t written = 0;
    while (written < (ssize_t)len) {
        ssize_t w = write(fd, data + written, len - written);
        if (w < 0) {
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "write(\"%s\") failed: %s", DATA_FILE, strerror(errno));
            close(fd);
            return -1;
        }
        written += w;
    }

    if (close(fd) == -1) {
        syslog(LOG_ERR, "close data file failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * Send entire contents of DATA_FILE to the client socket.
 * Returns 0 on success, -1 on error.
 */
static int send_file_contents(int client_fd)
{
    int fd = open(DATA_FILE, O_RDONLY);
    if (fd == -1) {
        syslog(LOG_ERR, "open(\"%s\") for read failed: %s", DATA_FILE, strerror(errno));
        return -1;
    }

    char buf[1024];
    ssize_t bytes;

    while ((bytes = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t sent_total = 0;
        while (sent_total < bytes) {
            ssize_t s = send(client_fd, buf + sent_total, bytes - sent_total, 0);
            if (s < 0) {
                if (errno == EINTR) {
                    continue;
                }
                syslog(LOG_ERR, "send() failed: %s", strerror(errno));
                close(fd);
                return -1;
            }
            sent_total += s;
        }
    }

    if (bytes < 0) {
        syslog(LOG_ERR, "read(\"%s\") failed: %s", DATA_FILE, strerror(errno));
        close(fd);
        return -1;
    }

    if (close(fd) == -1) {
        syslog(LOG_ERR, "close data file failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * Handle a single client connection:
 *  - Receive data until EOF, connection close, error, or exit_requested
 *  - Each time a newline-terminated packet is assembled:
 *      * append to file
 *      * send entire file back to client
 */
static void handle_client(int client_fd)
{
    char recv_buf[1024];

    char *packet_buf = NULL;     // dynamic buffer for partial/complete packets
    size_t packet_size = 0;      // bytes currently stored in packet_buf

    while (!exit_requested) {
        ssize_t bytes = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        if (bytes < 0) {
            if (errno == EINTR && exit_requested) {
                // Interrupted by signal and exit requested
                break;
            }
            if (errno == EINTR) {
                // Interrupted by signal but not necessarily exit, continue
                continue;
            }
            syslog(LOG_ERR, "recv() failed: %s", strerror(errno));
            break;
        } else if (bytes == 0) {
            // Client closed the connection
            break;
        }

        // Grow packet buffer
        char *new_buf = realloc(packet_buf, packet_size + bytes);
        if (!new_buf) {
            syslog(LOG_ERR, "realloc() failed while growing packet buffer");
            free(packet_buf);
            packet_buf = NULL;
            packet_size = 0;
            break;
        }
        packet_buf = new_buf;
        memcpy(packet_buf + packet_size, recv_buf, bytes);
        packet_size += bytes;

        // Process all complete packets (up to newline)
        size_t start = 0;
        for (size_t i = 0; i < packet_size; i++) {
            if (packet_buf[i] == '\n') {
                size_t packet_len = i - start + 1; // include '\n'

                if (append_to_file(packet_buf + start, packet_len) != 0) {
                    // Error logged in append_to_file()
                    // We can break and close the connection
                    start = i + 1;
                    break;
                }

                if (send_file_contents(client_fd) != 0) {
                    // Error logged in send_file_contents()
                    start = i + 1;
                    break;
                }

                start = i + 1;
            }
        }

        // Remove processed data from packet_buf (keep leftover partial packet, if any)
        if (start > 0 && start <= packet_size) {
            size_t remaining = packet_size - start;
            memmove(packet_buf, packet_buf + start, remaining);
            packet_size = remaining;

            char *shrink_buf = realloc(packet_buf, packet_size);
            if (shrink_buf || packet_size == 0) {
                packet_buf = shrink_buf;
            }
        }
    }

    free(packet_buf);
}

int main(int argc, char *argv[])
{
    int server_fd = -1;
    int ret = 0;

    // Open syslog
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Set up signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(SIGINT, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction(SIGINT) failed: %s", strerror(errno));
        ret = -1;
        goto cleanup;
    }
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction(SIGTERM) failed: %s", strerror(errno));
        ret = -1;
        goto cleanup;
    }

    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "socket() failed: %s", strerror(errno));
        ret = -1;
        goto cleanup;
    }

    // Allow reuse of address
    int optval = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval)) == -1) {
        syslog(LOG_ERR, "setsockopt(SO_REUSEADDR) failed: %s", strerror(errno));
        ret = -1;
        goto cleanup;
    }

    // Bind to port 9000
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        syslog(LOG_ERR, "bind() failed: %s", strerror(errno));
        ret = -1;
        goto cleanup;
    }

    // Listen
    if (listen(server_fd, BACKLOG) == -1) {
        syslog(LOG_ERR, "listen() failed: %s", strerror(errno));
        ret = -1;
        goto cleanup;
    }

    // Main accept loop
    while (!exit_requested) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            if (errno == EINTR && exit_requested) {
                // Interrupted by signal, exit requested
                break;
            }
            if (errno == EINTR) {
                // Interrupted by signal, continue
                continue;
            }

            syslog(LOG_ERR, "accept() failed: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN] = {0};
        if (!inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip))) {
            // Fallback if inet_ntop fails
            strncpy(client_ip, "unknown", sizeof(client_ip) - 1);
        }

        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        handle_client(client_fd);

        syslog(LOG_INFO, "Closed connection from %s", client_ip);

        if (close(client_fd) == -1) {
            syslog(LOG_ERR, "close(client_fd) failed: %s", strerror(errno));
        }
    }

    if (exit_requested) {
        syslog(LOG_INFO, "Caught signal, exiting");
    }

cleanup:
    if (server_fd != -1) {
        if (close(server_fd) == -1) {
            syslog(LOG_ERR, "close(server_fd) failed: %s", strerror(errno));
        }
    }

    // Remove data file on exit; ignore error if it doesn't exist
    if (remove(DATA_FILE) == -1 && errno != ENOENT) {
        syslog(LOG_ERR, "remove(\"%s\") failed: %s", DATA_FILE, strerror(errno));
    }

    closelog();
    return ret;
}

