#define _POSIX_C_SOURCE 200809L

#include "router.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <errno.h>
#include <limits.h>

const char* guess_mime(const char *path) {
    const char *ext = strrchr(path, '.');
    if (!ext) return "application/octet-stream";
    if (strcmp(ext, ".html") == 0) return "text/html";
    if (strcmp(ext, ".css") == 0) return "text/css";
    if (strcmp(ext, ".js") == 0) return "application/javascript";
    if (strcmp(ext, ".png") == 0) return "image/png";
    if (strcmp(ext, ".jpg") == 0 || strcmp(ext, ".jpeg") == 0) return "image/jpeg";
    if (strcmp(ext, ".gif") == 0) return "image/gif";
    if (strcmp(ext, ".txt") == 0) return "text/plain";
    return "application/octet-stream";
}

int serve_static(int fd, const char *root, const char *path, int is_head) {
    char full[PATH_MAX];
    snprintf(full, sizeof(full), "%s%s", root, path);

    int file_fd = open(full, O_RDONLY);
    if (file_fd < 0) return -1;

    struct stat st;
    if (fstat(file_fd, &st) < 0) {
        close(file_fd);
        return -1;
    }

    const char *mime = guess_mime(full);

    char header[512];
    int n = snprintf(header, sizeof(header),
                     "HTTP/1.1 200 OK\r\n"
                     "Content-Length: %ld\r\n"
                     "Content-Type: %s\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     st.st_size, mime);
    (void)write(fd, header, n);

    if (!is_head) {
        off_t offset = 0;
        while (offset < st.st_size) {
            ssize_t sent = sendfile(fd, file_fd, &offset, st.st_size - offset);
            if (sent <= 0) {
                if (errno == EINTR) continue;
                break;
            }
        }
    }

    close(file_fd);
    return 0;
}
