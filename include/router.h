#ifndef ROUTER_H
#define ROUTER_H

int serve_static(int fd, const char *docroot, const char *req_path);

#endif