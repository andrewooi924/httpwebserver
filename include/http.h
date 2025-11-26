#ifndef HTTP_H
#define HTTP_H

#include <unistd.h>

ssize_t read_until_double_crlf(int fd, char *buf, size_t cap);
void send_400(int fd);
void send_404(int fd);
void send_500(int fd);

#endif