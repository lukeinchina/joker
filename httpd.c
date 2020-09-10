/*
 * @brief:主要参考<<The linux programming interface>>实现.
 *        目的是通过动手实践理解http server的实现原理，和多进程的应用.
 */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
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

/*
 * 实现的几个简单的http 状态码.
 */
enum {
    STATUS_OK             = 200,
    STATUS_MOVED          = 301,
    STATUS_BAD_REQUEST    = 400,
    STATUS_NOT_FOUND      = 404,
    STATUS_INTERNAL_ERROR = 500,
    STATUS_UNDEFINED_METHOD = 501
};

/*--------------------------------------------------------------------*/
int serv_socket(uint16_t port);
int bad_client(int cli_fd);
void usage(const char *prog);
void undefined_method(int cli_fd);
void bad_request(int cli_fd);
void not_find(int cli_fd);
int internal_server_error(int cli_fd); /* 500 */
int cgi_exec_failed(int cli_fd);
int send_headers(int cli_fd); /* http head: 200 */

int read_all(int sockfd, char *buff, size_t size);

int send_file(int cli_fd, const char *path);
int exec_cgi(int cli_fd, const char *path, const char *query, const char *method);

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
        set_non_block(cli_fd);
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
    int err = 0;
    int n, length;
    char buff[4096];
    const char *query = NULL;
    while((n = readline(cli_fd, buff, sizeof(buff))) > 0) {
        printf("%s", buff); /* debug */
        if (0 == strcmp(buff, "\r\n")) {
            break;
        } else if (0 == strcasecmp(buff, "content-length:")) {
            /* HTTP1.0中这个字段可有可无 */
            length = atoi(buff + 15);
            printf("atoi:%d\n", length);
        }
    }
    if (n <= 0) {
        bad_request(cli_fd);
        return STATUS_BAD_REQUEST;
    }
    n = read(cli_fd, buff, sizeof(buff));
    if (n < 0) {
        return STATUS_BAD_REQUEST;
    }
    buff[n] = '\0';
    query = buff;
    err = exec_cgi(cli_fd, path, query, "POST");
    return (0 == err) ? STATUS_OK : STATUS_INTERNAL_ERROR;
}

int 
exec_cgi(int cli_fd, const char *path, const char *query, 
        const char *method) {
    int  n;
    char buff[4096];
    pid_t pid;
    int status;
    int cgi_infd[2];
    int cgi_outfd[2];

    char method_env[64];
    char length_env[64];
    char query_env[1024];

    if (pipe(cgi_infd) < 0 || pipe(cgi_outfd) < 0) {
        cgi_exec_failed(cli_fd);
        return -1;
    }
    if ((pid = fork()) < 0) {
        cgi_exec_failed(cli_fd);
        return -1;
    }
    /* 子进程执行cgi程序 */
    if (0 == pid) {
        close(cgi_infd[1]);
        close(cgi_outfd[0]);
        dup2(cgi_infd[0], 0);
        dup2(cgi_outfd[1], 1);
        snprintf(method_env, sizeof(method), "REQUEST_METHOD=%s", method);
        putenv(method_env);
        if (strcasecmp(method, "GET") == 0) {
            snprintf(query_env, sizeof(query_env), "QUERY_STRING=%s", query);
            putenv(query_env);
        } else {
            snprintf(length_env, sizeof(length_env), "CONTENT_LENGTH=%lu",
                    strlen(query));
            putenv(length_env);
        }
        execl(path, path, 0);
        exit(0);
    } else { /* 父进程，等待子进程退出*/
        close(cgi_infd[0]);
        close(cgi_outfd[1]);
        n = write(cgi_infd[1], query, strlen(query));
        n = read(cgi_outfd[0], buff, sizeof(buff));
        send_headers(cli_fd);
        if (n > 0) {
            write(cli_fd, buff, n);
        }
        close(cgi_infd[1]);
        close(cgi_outfd[0]);
        waitpid(pid, &status, 0);
    }

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
 */
int 
handle_request(int cli_fd) {
    char method[128];
    char uri[512];
    char buff[4096];
    size_t  i,j;
    int  len = 0;
    int  status = 200;

    /* 读出来第一行. */
    len = readline(cli_fd, buff, sizeof(buff));
    // len = read(cli_fd, buff, sizeof(buff));
    if (len == (int)(sizeof(buff) - 1) || len < 0) {
        bad_client(cli_fd);
        close(cli_fd);
        return -1;
    }

    /* 解析 method字段和URL字段 */
    i = 0;
    while (!isspace(buff[i]) && i < (size_t)len && i < sizeof(method) - 1) {
        method[i] = buff[i];
        i++;
    }
    method[i] = '\0';
    /* 跳过 GET 后面的空白 */
    while ('\0' != buff[i] && isspace(buff[i])) {
        i++;
    }
    /* 找到 url内容*/
    j = 0;
    while ('\0' != buff[i] && !isspace(buff[i]) && j < sizeof(uri)-1) {
        uri[j++] = buff[i++]; 
    }
    uri[j] = '\0'; /* url后面可能还有其他部分，截取掉。 */
    
    if (strcasecmp(method, "POST") == 0) {
        status = handle_post(cli_fd, uri);
    } else if (strcasecmp(method, "GET") == 0) {
        status = handle_get(cli_fd, uri);
    } else if (strcasecmp(method, "HEAD") == 0) {
        send_headers(cli_fd);
    } else {
        undefined_method(cli_fd);
        status = STATUS_UNDEFINED_METHOD;
    }
    close(cli_fd);
    return 0;
}

int 
bad_client(int cli_fd) {
    /* 可以输出对端的ip信息等 */
    fprintf(stderr, "bad client fd=%d, close\n", cli_fd);
    return 0;
}

void bad_request(int cli_fd) {
    char buff[1024];
    size_t off = 0;
	off += snprintf(buff+off, sizeof(buff)-off, "HTTP/1.0 400 BAD REQUEST\r\n");
	off += snprintf(buff+off, sizeof(buff)-off, HTTP_SERVER);
	off += snprintf(buff+off, sizeof(buff)-off, "Content-Type: text/html\r\n");
	off += snprintf(buff+off, sizeof(buff)-off, "\r\n");
	off += snprintf(buff+off, sizeof(buff)-off, "<P>Your browser sent a bad request,");
	off += snprintf(buff+off, sizeof(buff)-off, "such as a POST without a Content-Length.\r\n,");
	write(cli_fd, buff, off);
	return;
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
    write(cli_fd, buff, strlen(buff));
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
	write(cli_fd, buff, strlen(buff));
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
    int         err = 0;
    char        path[512];
    char        buff[4096];
    int         cgi   = 0;
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
        strcat(path, "/index.html"); /* ? out of range */
        stat(path, &st);
    }
    printf("rewrited path:%s\n", path);
    /* 文件不存在 */
    if (access(path, F_OK) != 0) {
        read_all(cli_fd, buff, sizeof(buff));
        fprintf(stderr, "can not find:%s\n", path);
        not_find(cli_fd);
        return STATUS_NOT_FOUND;
    }
    if (st.st_mode & S_IXUSR || st.st_mode & S_IXGRP || st.st_mode & S_IXOTH) {
        cgi = 1;
    }

    if (0 == cgi) {
        /* socket buffer里面内容读完。 */
        read_all(cli_fd, buff, sizeof(buff));
        err = send_file(cli_fd, path);
        return (0 == err) ? STATUS_OK : STATUS_NOT_FOUND;
    } else {
        err = exec_cgi(cli_fd, path, query, "GET");
        return (0 == err) ? STATUS_OK : STATUS_INTERNAL_ERROR;
    }
}

/*
 * @brief： 非阻塞文件描述符，读出socket中buffered全部数据。
 *
 */
int read_all(int sockfd, char *buff, size_t size) {
    int len    = 0;
    int offset = 0;
    int total  = 0;
    while (1) {
        /* 数据量太大，原来已经读到buffer的字节放弃 */
        if (offset >= (int)size) {
            offset = 0;
        }

        len = read(sockfd, buff + offset, size - offset);
        if (len == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("recv finish detected, quit...\n");
                break;
            }
        }
        offset += len;
        total  += len;
    }
    return total;
}

/*
 * @brief： 返回http 500错误，只写给client http head，不返回body
 *
 */
int 
internal_server_error(int cli_fd) {
    char buff[1024];
    int off = 0;
    int size = sizeof(buff);

    off += snprintf(buff+off, size-off, "HTTP/1.0 500 Internal Server Error\r\n");
    off += snprintf(buff+off, size-off, "Content-type: text/html\r\n");
    off += snprintf(buff+off, size-off, "\r\n");
    return write(cli_fd, buff, off);
}

int cgi_exec_failed(int cli_fd) {
    char buff[256] = {"<P>Error prohibited CGI execution.\r\n"} ;
    internal_server_error(cli_fd);
    return write(cli_fd, buff, strlen(buff)); 
}
