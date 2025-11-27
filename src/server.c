#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "http.h"
#include "router.h"
#include "threadpool.h"

#define PORT 8080
#define BACKLOG 128

ssize_t write_all(int fd, const void *buf, size_t count) {
    size_t written = 0;
    while (written < count) {
        ssize_t r = write(fd, (const char*)buf + written, count - written);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;  // permanent error
        }
        written += r;
    }
    return written;
}


void log_request(const http_request_t *req) {
    fprintf(stderr, "%s %s %s", req->method, req->path, req->version);
    if (req->query_count > 0) {
        fprintf(stderr, " [");
        for (int i = 0; i < req->query_count; i++) {
            if (i > 0) fprintf(stderr, ", ");
            fprintf(stderr, "%s=%s", req->query[i].key, req->query[i].value);
        }
        fprintf(stderr, "]");
    }
    fprintf(stderr, "\n");
}


void handle_connection(int fd) {
    http_request_t req;
    if (parse_http_request(fd, &req) < 0) {
        send_400(fd);
        close(fd);
        return;
    }
    if (strcmp(req.method, "GET") != 0 && 
        strcmp(req.method, "HEAD") != 0 &&
        strcmp(req.method, "POST") != 0) {
        send_400(fd);
        close(fd);
        return;
    }

    if (strcmp(req.path, "/") == 0) {
        strcpy(req.path, "/index.html");
    }

    log_request(&req);

    for (int i = 0; i < req.query_count; i++) {
        printf("Query param: %s = %s\n", req.query[i].key, req.query[i].value);
    }

    int is_head = strcmp(req.method, "HEAD") == 0;

    if (strcmp(req.method, "POST") == 0) {
        char header[256];
        int n = snprintf(header, sizeof(header),
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Length: %ld\r\n"
                        "Content-Type: text/plain\r\n"
                        "Connection: close\r\n"
                        "\r\n",
                        req.body_len);
        if (write_all(fd, header, n) < 0) perror("write_all header");
        if (write_all(fd, req.body, req.body_len) < 0) perror("write_all body");
    } else {
        int is_cgi = strncmp(req.path, "/cgi-bin/", 9) == 0;

        if (strcmp(req.method, "GET") == 0 && is_cgi) {
            char full[PATH_MAX];
            snprintf(full, sizeof(full), "www%s", req.path);

            int pipefd[2];
            if (pipe(pipefd) < 0) {
                send_500(fd);
            } else {
                pid_t pid = fork();
                if (pid < 0) {
                    send_500(fd);
                } else if (pid == 0) {
                    // Child process
                    close(pipefd[0]);
                    dup2(pipefd[1], STDOUT_FILENO);
                    close(pipefd[1]);
                    execl(full, full, NULL); // execute CGI script
                    exit(1); // if exec fails
                } else {
                    // Parent process
                    close(pipefd[1]);
                    char buf[8192];
                    ssize_t n;
                    // send HTTP headers first
                    const char *header = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n";
                    write(fd, header, strlen(header));

                    while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
                        write(fd, buf, n);
                    }
                    close(pipefd[0]);
                    waitpid(pid, NULL, 0);
                }
            }
        } else {
            // Static file delivery
            if (serve_static(fd, "www", req.path, is_head) < 0) {
                send_404(fd);
            }
        }
    }

    if (strcmp(req.method, "DELETE") == 0) {
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "www%s", req.path);

        if (unlink(full) == 0) {
            const char *resp = "HTTP/1.1 200 OK\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
            if (write_all(fd, resp, strlen(resp)) < 0) perror("write_all delete response");
        } else {
            send_404(fd);
        }
    }

    if (strcmp(req.method, "GET") == 0) {
        send_set_cookie(fd, "visited", "1");
    }

    free(req.body);
    close(fd);
}

int main(void) {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); return 1; }

    int flags = fcntl(listenfd, F_GETFL, 0);
    fcntl(listenfd, F_SETFL, flags | O_NONBLOCK);

    int opt = 1;
    setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(PORT);

    if (bind(listenfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (listen(listenfd, BACKLOG) < 0) { perror("listen"); return 1; }

    fprintf(stderr, "Listening on :%d\n", PORT);

    start_workers(8);
    int clients[1024];
    int client_count = 0;

    for (;;) {
        fd_set readfds;
        FD_ZERO(&readfds);
        FD_SET(listenfd, &readfds);

        int maxfd = listenfd;

        for (int i = 0; i < client_count; i++) {
            int fd = clients[i];
            FD_SET(fd, &readfds);
            if (fd > maxfd) maxfd = fd;
        }

        int nready = select(maxfd + 1, &readfds, NULL, NULL, NULL);
        if (nready < 0) {
            perror("select");
            continue;
        }

        // Accept new connections
        if (FD_ISSET(listenfd, &readfds)) {
            struct sockaddr_in cli;
            socklen_t cli_len = sizeof(cli);
            int conn = accept(listenfd, (struct sockaddr*)&cli, &cli_len);
            if (conn >= 0) {
                fcntl(conn, F_SETFL, O_NONBLOCK);
                clients[client_count++] = conn;
            }
        }

        // Handle ready clients
        for (int i = 0; i < client_count; i++) {
            int fd = clients[i];
            if (!FD_ISSET(fd, &readfds)) continue;

            char tmp;
            ssize_t r = recv(fd, &tmp, 1, MSG_PEEK);

            if (r == 0) {
                close(fd);
                clients[i] = clients[--client_count];
                i--;
                continue;
            }

            if (r < 0 && errno != EWOULDBLOCK && errno != EAGAIN) {
                close(fd);
                clients[i] = clients[--client_count];
                i--;
                continue;
            }

            push_conn(fd);
            clients[i] = clients[--client_count];
            i--;
        }
    }

    close(listenfd);
    return 0;
}
