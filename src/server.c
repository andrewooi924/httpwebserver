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
#include <sys/wait.h>
#include <sys/stat.h>
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

    if (strcmp(req.method, "POST") == 0 && strncmp(req.path, "/cgi-bin/", 9) == 0) {
        char full[PATH_MAX];
        snprintf(full, sizeof(full), "www%s", req.path);

        int pipefd[2];
        if (pipe(pipefd) < 0) { send_500(fd); close(fd); return; }

        pid_t pid = fork();
        if (pid < 0) { send_500(fd); close(fd); return; }

        if (pid == 0) {
            // Child: set up stdout to pipe
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);

            // Pass POST body via stdin
            int stdin_pipe[2];
            if (pipe(stdin_pipe) < 0) exit(1);
            pid_t pid2 = fork();
            if (pid2 == 0) {
                // Grandchild: execute CGI
                close(stdin_pipe[1]);
                dup2(stdin_pipe[0], STDIN_FILENO);
                close(stdin_pipe[0]);
                execl(full, full, NULL);
                exit(1);
            } else {
                // Child: write POST data to stdin
                close(stdin_pipe[0]);
                write(stdin_pipe[1], req.body, req.body_len);
                close(stdin_pipe[1]);
                waitpid(pid2, NULL, 0);
                exit(0);
            }
        } else {
            // Parent: read CGI output
            close(pipefd[1]);
            char buf[4096];
            ssize_t n;
            // Send minimal HTTP headers first
            const char *header = "HTTP/1.1 200 OK\r\nConnection: close\r\n\r\n";
            write(fd, header, strlen(header));
            while ((n = read(pipefd[0], buf, sizeof(buf))) > 0) {
                write(fd, buf, n);
            }
            close(pipefd[0]);
            waitpid(pid, NULL, 0);
        }
    } else {
        const char *ctype = NULL;
        for (int i = 0; i < req.header_count; i++) {
            if (strcasecmp(req.headers[i].name, "Content-Type") == 0) {
                ctype = req.headers[i].value;
                break;
            }
        }

        if (ctype && strncmp(ctype, "multipart/form-data;", 20) == 0) {
            const char *bstr = strstr(ctype, "boundary=");
            if (!bstr) {
                send_400(fd);
            } else {
                char boundary[128];
                snprintf(boundary, sizeof(boundary), "--%s", bstr + 9); // prefix "--"

                char *pos = req.body;
                char *end = req.body + req.body_len;

                mkdir("www/uploads", 0755); // ensure upload dir exists

                while (pos < end) {
                    char *part_start = strstr(pos, boundary);
                    if (!part_start) break;
                    part_start += strlen(boundary);
                    if (strncmp(part_start, "\r\n", 2) == 0) part_start += 2;

                    // find next boundary
                    char *part_end = strstr(part_start, boundary);
                    if (!part_end) break;

                    // find headers
                    char *header_end = strstr(part_start, "\r\n\r\n");
                    if (!header_end) break;
                    char *content = header_end + 4;

                    // parse filename
                    char filename[256] = {0};
                    char *cd = strstr(part_start, "Content-Disposition:");
                    if (cd) {
                        char *fn = strstr(cd, "filename=\"");
                        if (fn) {
                            fn += 10;
                            char *q = strchr(fn, '"');
                            if (q) *q = 0;
                            strncpy(filename, fn, sizeof(filename) - 1);
                        }
                    }

                    // calculate content length safely
                    size_t content_len = part_end - content;
                    if (content_len >= 2 && content[content_len - 2] == '\r' && content[content_len - 1] == '\n') {
                        content_len -= 2;
                    }

                    if (filename[0] && content_len > 0) {
                        char fullpath[PATH_MAX];
                        snprintf(fullpath, sizeof(fullpath), "www/uploads/%s", filename);
                        FILE *f = fopen(fullpath, "wb");
                        if (f) {
                            fwrite(content, 1, content_len, f);
                            fclose(f);
                        }
                    }

                    pos = part_end;
                }

                const char *resp = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nUploaded successfully";
                write(fd, resp, strlen(resp));
            }
        }
        else {
            // fallback: echo POST body
            char header[256];
            int n = snprintf(header, sizeof(header),
                            "HTTP/1.1 200 OK\r\n"
                            "Content-Length: %ld\r\n"
                            "Content-Type: text/plain\r\n"
                            "Connection: close\r\n"
                            "\r\n",
                            req.body_len);
            write(fd, header, n);
            write(fd, req.body, req.body_len);
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
