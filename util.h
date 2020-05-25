#ifndef JOKER_UTIL_H_
#define JOKER_UTIL_H_

#include <stdio.h>
#include <stdlib.h>

int set_non_block(int sock);
void error_exit(const char *str);

ssize_t readn(int fd, void *vptr, size_t n);
ssize_t writen(int fd, const void *vptr, size_t n);
ssize_t readline(int fd, void *vptr, size_t maxlen);
#endif
