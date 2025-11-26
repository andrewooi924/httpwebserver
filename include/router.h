#ifndef ROUTER_H
#define ROUTER_H

int serve_static(int fd, const char *root, const char *path, int is_head);
const char* guess_mime(const char *path);

#endif
