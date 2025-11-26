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

void handle_connection(int fd) {
    char buf[8192];
    ssize_t r = read_until_double_crlf(fd, buf, sizeof(buf));
    if (r <= 0) { close(fd); return; }

    char method[16], path[1024], ver[16];
    if (sscanf(buf, "%15s %1023s %15s", method, path, ver) != 3) {
        send_400(fd);
        close(fd);
        return;
    }
    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        send_400(fd);
        close(fd);
        return;
    }

    if (strcmp(path, "/") == 0) {
        strcpy(path, "/index.html");
    }

    if (serve_static(fd, "www", path) < 0) {
        send_404(fd);
    }

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
