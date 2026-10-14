#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define PORT 9000
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUF_SIZE 1024

static volatile sig_atomic_t caught_signal = 0;

static void signal_handler(int signo)
{
    (void)signo;
    caught_signal = 1;
}

/* Append packet */
static int append_packet(const char *data, size_t len)
{
    int fd = open(DATA_FILE, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd == -1) {
        syslog(LOG_ERR, "open %s for append failed: %s", DATA_FILE, strerror(errno));
        return -1;
    }
    size_t written = 0;
    while (written < len) {
        ssize_t rc = write(fd, data + written, len - written);
        if (rc == -1) {
            syslog(LOG_ERR, "write failed: %s", strerror(errno));
            close(fd);
            return -1;
        }
        written += rc;
    }
    close(fd);
    return 0;
}

/* Send file */
static int send_file_contents(int client_fd)
{
    char buf[BUF_SIZE];
    int fd = open(DATA_FILE, O_RDONLY);
    if (fd == -1) {
        syslog(LOG_ERR, "open %s for read failed: %s", DATA_FILE, strerror(errno));
        return -1;
    }
    ssize_t bytes_read;
    while ((bytes_read = read(fd, buf, sizeof(buf))) > 0) {
        ssize_t sent = 0;
        while (sent < bytes_read) {
            ssize_t rc = send(client_fd, buf + sent, bytes_read - sent, 0);
            if (rc == -1) {
                syslog(LOG_ERR, "send failed: %s", strerror(errno));
                close(fd);
                return -1;
            }
            sent += rc;
        }
    }
    if (bytes_read == -1) {
        syslog(LOG_ERR, "read failed: %s", strerror(errno));
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

/* Handle client */
static void handle_client(int client_fd)
{
    char recv_buf[BUF_SIZE];
    char *packet = NULL;
    size_t packet_len = 0;

    while (!caught_signal) {
        ssize_t bytes = recv(client_fd, recv_buf, sizeof(recv_buf), 0);
        if (bytes == 0) {
            break;
        }
        if (bytes == -1) {
            if (errno == EINTR) {
                continue;
            }
            syslog(LOG_ERR, "recv failed: %s", strerror(errno));
            break;
        }

        char *new_packet = realloc(packet, packet_len + bytes);
        if (new_packet == NULL) {
            syslog(LOG_ERR, "realloc failed, discarding packet");
            free(packet);
            packet = NULL;
            packet_len = 0;
            continue;
        }
        packet = new_packet;
        memcpy(packet + packet_len, recv_buf, bytes);
        packet_len += bytes;

        char *newline;
        while ((newline = memchr(packet, '\n', packet_len)) != NULL) {
            size_t line_len = newline - packet + 1;
            if (append_packet(packet, line_len) == 0) {
                send_file_contents(client_fd);
            }
            memmove(packet, packet + line_len, packet_len - line_len);
            packet_len -= line_len;
        }
    }
    free(packet);
}

/* Daemonize after bind */
static int daemonize(void)
{
    pid_t pid = fork();
    if (pid == -1) {
        return -1;
    }
    if (pid > 0) {
        exit(0);
    }
    if (setsid() == -1) {
        return -1;
    }
    if (chdir("/") == -1) {
        return -1;
    }
    int devnull = open("/dev/null", O_RDWR);
    if (devnull == -1) {
        return -1;
    }
    dup2(devnull, STDIN_FILENO);
    dup2(devnull, STDOUT_FILENO);
    dup2(devnull, STDERR_FILENO);
    if (devnull > STDERR_FILENO) {
        close(devnull);
    }
    return 0;
}

int main(int argc, char *argv[])
{
    int daemon_mode = (argc == 2 && strcmp(argv[1], "-d") == 0);

    openlog("aesdsocket", 0, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = signal_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        syslog(LOG_ERR, "sigaction failed: %s", strerror(errno));
        closelog();
        return -1;
    }

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "socket failed: %s", strerror(errno));
        closelog();
        return -1;
    }

    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        syslog(LOG_ERR, "setsockopt failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(PORT);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "bind failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    if (daemon_mode && daemonize() == -1) {
        syslog(LOG_ERR, "daemonize failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    if (listen(server_fd, 10) == -1) {
        syslog(LOG_ERR, "listen failed: %s", strerror(errno));
        close(server_fd);
        closelog();
        return -1;
    }

    while (!caught_signal) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            if (errno != EINTR) {
                syslog(LOG_ERR, "accept failed: %s", strerror(errno));
            }
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        handle_client(client_fd);

        close(client_fd);
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    syslog(LOG_INFO, "Caught signal, exiting");
    close(server_fd);
    remove(DATA_FILE);
    closelog();
    return 0;
}
