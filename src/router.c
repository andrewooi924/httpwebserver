#define _POSIX_C_SOURCE 200809L

#include <limits.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/sendfile.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "router.h"
#include "http.h"


extern const char *guess_mime(const char *);

int serve_static(int fd, const char *docroot, const char *req_path) {
    char full[PATH_MAX];
    if (req_path[0] != '/') return -1;
    // Normalize: prevent header like /../../
    char rel[PATH_MAX];
    snprintf(rel, sizeof(rel), "%s%s", docroot, req_path);
    // if path ends with '/', append index.html
    if (rel[strlen(rel)-1] == '/') strncat(rel, "index.html", sizeof(rel)-strlen(rel)-1);

    if (!realpath(rel, full)) return -1;

    // ensure `full` starts with docroot realpath
    char root_resolved[PATH_MAX];
    if (!realpath(docroot, root_resolved)) return -1;
    if (strncmp(full, root_resolved, strlen(root_resolved)) != 0) return -1;

    struct stat st;
    if (stat(full, &st) < 0) return -1;
    if (!S_ISREG(st.st_mode)) return -1;

    int fdfile = open(full, O_RDONLY);
    if (fdfile < 0) return -1;

    const char *mime = guess_mime(full);
    char hdr[512];
    int n = snprintf(hdr, sizeof(hdr),
                     "HTTP/1.0 200 OK\r\n"
                     "Content-Length: %lld\r\n"
                     "Content-Type: %s\r\n"
                     "Connection: close\r\n"
                     "\r\n",
                     (long long)st.st_size, mime);
    (void)write(fd, hdr, n);

    off_t off = 0;
    while (off < st.st_size) {
        ssize_t sent = sendfile(fd, fdfile, &off, st.st_size - off);
        if (sent <= 0) { if (errno == EINTR) continue; break; }
    }
    close(fdfile);
    return 0;
}
