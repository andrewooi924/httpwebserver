#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "http.h"
#include "router.h"
#include "threadpool.h"

#define PORT 8080
#define BACKLOG 128

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
        write(fd, header, n);
        write(fd, req.body, req.body_len);
    } else {
        if (serve_static(fd, "www", req.path, is_head) < 0) {
            send_404(fd);
        }
    }

    free(req.body); // free if POST body allocated

    close(fd);
}

int main(void) {
    int listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) { perror("socket"); return 1; }

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
    for (;;) {
        struct sockaddr_in cli;
        socklen_t cli_len = sizeof(cli);
        int conn = accept(listenfd, (struct sockaddr*)&cli, &cli_len);
        if (conn < 0) { perror("accept"); continue; }
        push_conn(conn);
    }

    close(listenfd);
    return 0;
}
