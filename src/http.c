#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <errno.h>
#include "http.h"
#include "router.h"

#define BUF_SIZE 8192

ssize_t read_until_double_crlf(int fd, char *buf, size_t cap) {
    size_t used = 0;
    while (used < cap - 1) {
        ssize_t r = recv(fd, buf + used, cap - 1 - used, 0);
        if (r <= 0) return r;
        used += r;
        buf[used] = 0;
        if (strstr(buf, "\r\n\r\n")) return used;
    }
    return -2; // too large
}

void send_400(int fd) {
    const char *s = "HTTP/1.0 400 Bad Request\r\nContent-Length:11\r\nConnection: close\r\n\r\nBad Request\n";
    (void)write(fd, s, strlen(s));
}
void send_404(int fd) {
    const char *s = "HTTP/1.0 404 Not Found\r\nContent-Length:10\r\nConnection: close\r\n\r\nNot Found\n";
    (void)write(fd, s, strlen(s));
}
void send_500(int fd) {
    const char *s = "HTTP/1.0 500 Internal\r\nContent-Length:16\r\nConnection: close\r\n\r\nInternal Error\n";
    (void)write(fd, s, strlen(s));
}

const char *guess_mime(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (!strcmp(ext, ".html")) return "text/html";
    if (!strcmp(ext, ".htm")) return "text/html";
    if (!strcmp(ext, ".css")) return "text/css";
    if (!strcmp(ext, ".js")) return "application/javascript";
    if (!strcmp(ext, ".png")) return "image/png";
    if (!strcmp(ext, ".jpg") || !strcmp(ext, ".jpeg")) return "image/jpeg";
    if (!strcmp(ext, ".txt")) return "text/plain";
    return "application/octet-stream";
}
