#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <ctype.h>
#include <assert.h>
#include <pthread.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>

#include "util.h"

#define HTTP_SERVER "Server: httpd 0.1.0\r\n"

enum {
    METHOD_GET = 1,
    METHOD_POST = 2
};

/*--------------------------------------------------------------------*/
int serv_socket(uint16_t port);
int bad_client(int cli_fd);
void usage(const char *prog);
void undefined_method(int cli_fd);
void not_find(int cli_fd);

int discard_left(int cli_fd, char *buff, size_t size);

int send_file(int cli_fd, const char *path);
int exec_cgi(int cli_fd, const char *path, const char *query);

int handle_get(int cli_fd, const char *uri);
int handle_post(int cli_fd, const char *uri);
int handle_request(int cli_fd);
/*--------------------------------------------------------------------*/
int
main(int argc, char *argv[]) {
    if (argc < 2) {
        usage(argv[0]);
        exit(0);
    }
    struct sockaddr_in cli_sin;
    int cli_fd    = -1;
    socklen_t len = sizeof(cli_sin);
    int port      = atoi(argv[1]);
    int serv_fd   = serv_socket(port);
    printf("httpd running on port %d\n", port);
    while (1) {
        cli_fd = accept(serv_fd, (struct sockaddr *)&cli_sin, &len);
        if (cli_fd < 0) {
            perror("accept client connect error:");
            continue;
        }
        handle_request(cli_fd);
    }
    close(serv_fd);
    return 0;
}

void usage(const char *prog) {
    printf("usage:%s port\n", prog);
    return ;
}

int serv_socket(uint16_t port) {
    struct sockaddr_in sin;
    int fd = socket(PF_INET, SOCK_STREAM, 0);;
    if (fd < 0) {
        error_exit("listen failed");
    }
    memset(&sin, 0, sizeof(sin));
	sin.sin_family      = AF_INET;
	sin.sin_port        = htons(port);
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(fd, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
        error_exit("bind failed");
    }
    /* BSD socket 实现中，backlog 的上限是5 */
    if (listen(fd, 5) < 0) {
        error_exit("listen failed");
    }
	return fd;
}

/*
 * POST 方法举例：
 * POST /listNews http/1.1
 * Host: 127.0.0.1:8080
 * User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_12_6) Safari/537.36
 * Accept: application/json
 * Content-Type: application/x-www-form-urlencoded
 * 
 * pageIndex=1&pageSize=20
 *
 * note: 这里只处理"Content-Type: application/x-www-form-urlencoded" 一种类型
 *       其他类型如"Content-Type: application/json", 暂不支持
 */
int 
handle_post(int cli_fd, const char *path) {
    ;
    return 0;
}

int 
exec_cgi(int cli_fd, const char *path, const char *query) {
    ;
    return 0;
}

/* 
 * GET 方法样例:
 *
 * GET /listNews http/1.1
 * Host: 127.0.0.1:8080
 * User-Agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_12_6) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/60.0.3112.113 Safari/537.36
 * Accept: application/json
 *
 *
 */
int 
handle_request(int cli_fd) {
    char *query;
    char *p;
    char method[128];
    char uri[512];
    char buff[4096];
    size_t  i,j;
    size_t  len = 0;
    int  cgi = 0;

    /* 读出来第一行. */
    len = readline(cli_fd, buff, sizeof(buff));
    if (len >= sizeof(buff)) {
        bad_client(cli_fd);
    }
    printf("first line:\n%s\n", buff);

    /* 解析 method字段和URL字段 */
    i = 0;
    while (!isspace(buff[i]) && i < len && i < sizeof(method) - 1) {
        method[i] = buff[i];
        i++;
    }
    method[i] = '\0';
    printf("method:%s\n", method);
    while ('\0' != buff[i] && isspace(buff[i])) {
        i++;
    }
    j = 0;
    while ('\0' != buff[i] && !isspace(buff[i]) && j < sizeof(uri)-1) {
        uri[j++] = buff[i++];
    }
    uri[j] = '\0';
    printf("uri:%s\n", uri);
    
    if (strcasecmp(method, "POST") == 0) {
        return handle_post(cli_fd, uri);
    } else if (strcasecmp(method, "GET") == 0) {
        return handle_get(cli_fd, uri);
    } else {
        undefined_method(cli_fd);
        return 1;
    }
    return 0;
}

int 
bad_client(int cli_fd) {
    fprintf(stderr, "bad client, close\n");
    close(cli_fd);
    return 0;
}

void 
undefined_method(int cli_fd) {
    char buff[1024];
    size_t off = 0;

    off += snprintf(buff+off, sizeof(buff)-off, "HTTP/1.0 501 Method Not Implemented\r\n");
    off += snprintf(buff+off, sizeof(buff)-off, HTTP_SERVER);
    off += snprintf(buff+off, sizeof(buff)-off, "Content-Type: text/html\r\n");
    off += snprintf(buff+off, sizeof(buff)-off, "\r\n");
    off += snprintf(buff+off, sizeof(buff)-off, "<HTML><HEAD><TITLE>Method Not Implemented\r\n");
    off += snprintf(buff+off, sizeof(buff)-off, "</TITLE></HEAD>\r\n");
    off += snprintf(buff+off, sizeof(buff)-off, "<BODY><P>HTTP request method not supported.\r\n");
    off += snprintf(buff+off, sizeof(buff)-off, "</BODY></HTML>\r\n");
    send(cli_fd, buff, strlen(buff), 0);
    close(cli_fd);
}

void 
not_find(int cli_fd) {
    char buff[1024];
    size_t off = 0;
	off += snprintf(buff+off, sizeof(buff)-off, "HTTP/1.0 404 NOT FOUND\r\n");
	off += snprintf(buff+off, sizeof(buff)-off, HTTP_SERVER);
	off += snprintf(buff+off, sizeof(buff)-off, "Content-Type: text/html\r\n");
	off += snprintf(buff+off, sizeof(buff)-off, "\r\n");
	off += snprintf(buff+off, sizeof(buff)-off, "<HTML><TITLE>Not Found</TITLE>\r\n");
	off += snprintf(buff+off, sizeof(buff)-off, "<BODY><P>The server could not fulfill\r\n");
	off += snprintf(buff+off, sizeof(buff)-off, "your request because the resource specified\r\n");
	off += snprintf(buff+off, sizeof(buff)-off, "is unavailable or nonexistent.\r\n");
	off += snprintf(buff+off, sizeof(buff)-off, "</BODY></HTML>\r\n");
	send(cli_fd, buff, strlen(buff), 0);
    return;
}

int 
send_headers(int cli_fd) {
    char buff[1024];
    ssize_t off = 0;
    size_t size = sizeof(buff);
	off += snprintf(buff+off, size - off,  "HTTP/1.0 200 OK\r\n");
	off += snprintf(buff+off, size - off,  HTTP_SERVER);
	off += snprintf(buff+off, size - off, "Content-Type: text/html; charset=utf-8\r\n");
	off += snprintf(buff+off, size - off,  "\r\n");
    fprintf(stderr, "response header:\n%s", buff);
    return writen(cli_fd, buff, off);
}

int 
send_file(int cli_fd, const char *path) {
    FILE *fp = NULL;
    int fd = -1;
	int n  = 1;
	char buff[4096];

    /* socket buffer里面内容读完。 */
    discard_left(cli_fd, buff, sizeof(buff));

	fp = fopen(path, "r");
    if (NULL == fp) {
		not_find(cli_fd);
        return -1;
    }
    fd = fileno(fp);
    send_headers(cli_fd);
    //接着把这个文件的内容读出来作为 response 的 body 发送到客户端
    while ((n = read(fd, buff, sizeof(buff))) > 0) {
        writen(cli_fd, buff, n);
    }

	fclose(fp);
    return 0;
}

int handle_get(int cli_fd, const char *uri) {
    char        path[512];
    char        buff[4096];
    int         cgi   = 0;
    size_t      len   = 0;
    const char *query = NULL;
    const char *p     = uri;
    struct stat st;

    /*GET method: GET /listNews?pageSize=20&pageIndex=1 */
    while ('\0' != *p && '?' != *p) {
        p++;
    }
    assert((size_t)(p-uri) < sizeof(path));
    memcpy(path, uri, p - uri);
    path[p-uri] = '\0';
    /* 是cgi， 跳过? 找到参数 */
    if ('?' == *p) {
        cgi  = 1;
        query       = ++p;
    }

    printf("uri:%s\n",  uri);
    printf("path:%s\n", path);
    stat(path, &st);
    if (S_ISDIR(st.st_mode)) {
        snprintf(path, sizeof(path), "%s/index.html", uri);
        stat(path, &st);
    }
    printf("rewrited uri:%s\n", uri);
    /* 文件不存在 */
    if (access(path, F_OK) != 0) {
        discard_left(cli_fd, buff, sizeof(buff));
        fprintf(stderr, "can not find:%s\n", path);
        not_find(cli_fd);
        close(cli_fd);
    }
    if (st.st_mode & S_IXUSR || st.st_mode & S_IXGRP || st.st_mode & S_IXOTH) {
        cgi = 1;
    }

    if (0 == cgi) {
        send_file(cli_fd, path);
    } else {
        exec_cgi(cli_fd, path, query);
    }
    close(cli_fd);
    return 0;
}

/*
 * @brief:读出socket中的内容，丢弃.
 * @return : succ:0, failed: -1
 */
int 
discard_left(int cli_fd, char *buff, size_t size) {
    size_t len = 0;
    do {
        len = read(cli_fd, buff, size);
    } while (size == len);
    return 0; 
}
