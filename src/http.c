#include "http.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <ctype.h>

void parse_query_string(http_request_t *req) {
    req->query_count = 0;
    char *q = strchr(req->path, '?');
    if (!q) return;

    *q = 0; // terminate path before ?
    q++;

    char *pair = strtok(q, "&");
    while (pair && req->query_count < MAX_QUERY_PARAMS) {
        char *eq = strchr(pair, '=');
        if (!eq) { pair = strtok(NULL, "&"); continue; }

        *eq = 0;
        strncpy(req->query[req->query_count].key, pair, 63);
        strncpy(req->query[req->query_count].value, eq + 1, 255);
        req->query_count++;
        pair = strtok(NULL, "&");
    }
}

ssize_t read_until_double_crlf(int fd, char *buf, size_t cap) {
    size_t used = 0;
    while (used < cap - 1) {
        ssize_t r = read(fd, buf + used, 1);
        if (r <= 0) break;
        used += r;
        if (used >= 4 && 
            buf[used-4] == '\r' && buf[used-3] == '\n' &&
            buf[used-2] == '\r' && buf[used-1] == '\n') {
            buf[used] = 0;
            return used;
        }
    }
    buf[used] = 0;
    return used;
}

int parse_http_request(int fd, http_request_t *req) {
    char buf[8192];
    ssize_t n = read_until_double_crlf(fd, buf, sizeof(buf));
    if (n <= 0) return -1;

    char *line = strtok(buf, "\r\n");
    if (!line) return -1;

    // Parse request line
    if (sscanf(line, "%15s %1023s %15s", req->method, req->path, req->version) != 3) return -1;

    parse_query_string(req);

    // Parse headers
    req->header_count = 0;
    while ((line = strtok(NULL, "\r\n")) && req->header_count < MAX_HEADERS) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = 0;
        // trim spaces
        char *name = line;
        while (*name && isspace(*name)) name++;
        char *value = colon + 1;
        while (*value && isspace(*value)) value++;

        strncpy(req->headers[req->header_count].name, name, 63);
        strncpy(req->headers[req->header_count].value, value, 255);
        req->header_count++;
    }

    // Check for body (Content-Length)
    req->body = NULL;
    req->body_len = 0;
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, "Content-Length") == 0) {
            req->body_len = strtoul(req->headers[i].value, NULL, 10);
            if (req->body_len > 0) {
                req->body = malloc(req->body_len + 1);
                if (!req->body) return -1;
                size_t received = 0;
                while (received < req->body_len) {
                    ssize_t r = read(fd, req->body + received, req->body_len - received);
                    if (r <= 0) break;
                    received += r;
                }
                req->body[received] = 0;
                req->body_len = received;
            }
            break;
        }
    }

    return 0;
}

void send_400(int fd) {
    const char *s = "HTTP/1.1 400 Bad Request\r\nContent-Length: 11\r\n\r\nBad Request";
    write(fd, s, strlen(s));
}

void send_404(int fd) {
    const char *s = "HTTP/1.1 404 Not Found\r\nContent-Length: 9\r\n\r\nNot Found";
    write(fd, s, strlen(s));
}

void send_500(int fd) {
    const char *s = "HTTP/1.1 500 Internal Server Error\r\nContent-Length: 21\r\n\r\nInternal Server Error";
    write(fd, s, strlen(s));
}
