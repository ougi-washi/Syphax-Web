#ifndef SW_SERVER_H
#define SW_SERVER_H

#include "sw_types.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>

#define SW_MAX_HTTP_HEADERS 40
#define SW_MAX_HTTP_HEADER_SIZE 4096
#define SW_MAX_URL_SIZE 2048
#define SW_MAX_VAR_SIZE 1024
#define SW_MAX_READ_SIZE 8192
#define SW_MAX_WRITE_SIZE 8192
#define SW_MAX_MULTIPART_SIZE 65536
#define SW_MAX_CONNS 1000

typedef struct sw_str {
    c8 *ptr;
    sz len;
} sw_str_t;

typedef struct sw_http_header {
    c8 *name;
    c8 *value;
} sw_http_header_t;

typedef struct sw_http_message {
    c8 *method;
    c8 *uri;
    c8 *proto;
    sw_http_header_t headers[SW_MAX_HTTP_HEADERS];
    i32 num_headers;
    c8 *body;
    sz body_len;
    sz content_length;
    i32 is_chunked;
} sw_http_message_t;

typedef struct sw_connection {
    i32 sock;
    struct sockaddr_in addr;
    c8 read_buf[SW_MAX_READ_SIZE];
    sz read_len;
    c8 write_buf[SW_MAX_WRITE_SIZE];
    sz write_len;
    sw_http_message_t *request;
    void *user_data;
    void (*handler)(struct sw_connection *);
    i32 is_draining;
    time_t last_activity;
} sw_connection_t;

typedef struct sw_mgr {
    i32 epoll_fd;
    sw_connection_t *conns[SW_MAX_CONNS];
    i32 num_conns;
    void (*http_handler)(sw_connection_t *, sw_http_message_t *);
} sw_mgr_t;

typedef struct sw_http_multipart {
    c8 *boundary;
    c8 *data;
    sz data_len;
    c8 *name;
    c8 *filename;
    c8 *content_type;
} sw_http_multipart_t;

i32 sw_mgr_init(sw_mgr_t *mgr);
i32 sw_http_listen(sw_mgr_t *mgr, const c8 *url);
i32 sw_mgr_poll(sw_mgr_t *mgr, i32 timeout_ms);
void sw_mgr_free(sw_mgr_t *mgr);
void sw_mgr_set_http_handler(sw_mgr_t *mgr, void (*handler)(sw_connection_t *, sw_http_message_t *));

i32 sw_http_get_var(sw_http_message_t *hm, const c8 *name, c8 *buf, sz buf_len);
i32 sw_http_reply(sw_connection_t *c, i32 status_code, const c8 *headers, const c8 *fmt, ...);
i32 sw_printf(sw_connection_t *c, const c8 *fmt, ...);
i32 sw_match(const c8 *pattern, const c8 *str);
i32 sw_http_serve_file(sw_connection_t *c, const c8 *path);
i32 sw_http_next_multipart(sw_http_message_t *hm, sw_http_multipart_t *mp, sz *offset);

void sw_http_parse_request(sw_connection_t *c);
i32 sw_http_parse_headers(sw_connection_t *c);
void sw_http_handle_request(sw_connection_t *c);

#endif // SW_SERVER_H
