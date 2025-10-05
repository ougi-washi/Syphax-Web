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

typedef struct {
    c8 *ptr;
    sz len;
} sw_str;

typedef struct {
    c8 *name;
    c8 *value;
} sw_http_header;

typedef struct {
    c8 *method;
    c8 *uri;
    c8 *proto;
    sw_http_header headers[SW_MAX_HTTP_HEADERS];
    i32 num_headers;
    c8 *body;
    sz body_len;
    sz content_length;
    i32 is_chunked;
} sw_http_message;

typedef struct sw_connection {
    i32 sock;
    struct sockaddr_in addr;
    c8 read_buf[SW_MAX_READ_SIZE];
    sz read_len;
    c8 write_buf[SW_MAX_WRITE_SIZE];
    sz write_len;
    sw_http_message *request;
    void *user_data;
    void (*handler)(struct sw_connection *);
    i32 is_draining;
    time_t last_activity;
} sw_connection;

typedef struct {
    i32 epoll_fd;
    sw_connection *conns[SW_MAX_CONNS];
    i32 num_conns;
    void (*http_handler)(sw_connection *, sw_http_message *);
} sw_mgr;

typedef struct {
    c8 *boundary;
    c8 *data;
    sz data_len;
    c8 *name;
    c8 *filename;
    c8 *content_type;
} sw_http_multipart;

extern b8 sw_server_init(void (*http_handler)(sw_connection *, sw_http_message *));
extern void sw_server_listen(const c8 *addr);
extern void sw_server_clear();

extern i32 sw_mgr_init(sw_mgr *mgr);
extern i32 sw_http_listen(sw_mgr *mgr, const c8 *url);
extern i32 sw_mgr_poll(sw_mgr *mgr, i32 timeout_ms);
extern void sw_mgr_free(sw_mgr *mgr);
extern void sw_mgr_set_http_handler(sw_mgr *mgr, void (*handler)(sw_connection *, sw_http_message *));

extern i32 sw_http_get_var(sw_http_message *hm, const c8 *name, c8 *buf, sz buf_len);
extern i32 sw_http_reply(sw_connection *c, i32 status_code, const c8 *headers, const c8 *fmt, ...);
extern i32 sw_printf(sw_connection *c, const c8 *fmt, ...);
extern i32 sw_match(const c8 *pattern, const c8 *str);
extern i32 sw_http_serve_file(sw_connection *c, const c8 *path);
extern i32 sw_http_next_multipart(sw_http_message *hm, sw_http_multipart *mp, sz *offset);

extern void sw_http_parse_request(sw_connection *c);
extern i32 sw_http_parse_headers(sw_connection *c);
extern void sw_http_handle_request(sw_connection *c);

#endif // SW_SERVER_H
