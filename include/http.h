#ifndef HTTP_H
#define HTTP_H

#include <unistd.h>
#include <stddef.h>

#define MAX_HEADERS 32
#define MAX_QUERY_PARAMS 32

typedef struct {
    char method[16];
    char path[1024];
    char version[16];
    struct {
        char name[64];
        char value[256];
    } headers[MAX_HEADERS];
    int header_count;

    struct {
        char key[64];
        char value[256];
    } query[MAX_QUERY_PARAMS];
    int query_count;
    
    char *body;
    size_t body_len;
} http_request_t;

ssize_t read_until_double_crlf(int fd, char *buf, size_t cap);

int parse_http_request(int fd, http_request_t *req);

void send_400(int fd);
void send_404(int fd);
void send_500(int fd);

#endif
