#ifndef SW_SERVER_H
#define SW_SERVER_H

#include "sw_export.h"
#include "syphax/s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sw_mgr sw_mgr;
typedef struct sw_connection sw_connection;

typedef struct {
    const c8* name;
    const c8* value;
} sw_http_header;

typedef struct {
    const c8* method;
    const c8* uri;
    const c8* proto;
    sw_http_header* headers;
    sz num_headers;
    const c8* body;
    sz body_len;
    sz content_length;
    b8 is_chunked;
} sw_http_message;

typedef struct {
    const c8* boundary;
    const c8* data;
    sz data_len;
    c8* name;
    c8* filename;
    c8* content_type;
} sw_http_multipart;

typedef void (*sw_http_handler)(sw_connection* connection, const sw_http_message* request, void* user_data);

SW_API sw_mgr* sw_mgr_create(void);
SW_API void sw_mgr_destroy(sw_mgr* mgr);
SW_API i32 sw_http_listen(sw_mgr* mgr, const c8* url, sw_http_handler handler, void* user_data);
SW_API i32 sw_mgr_poll(sw_mgr* mgr, i32 timeout_ms);
SW_API void sw_mgr_request_stop(sw_mgr* mgr);
SW_API b8 sw_mgr_is_running(const sw_mgr* mgr);
SW_API u16 sw_mgr_get_listener_port(const sw_mgr* mgr, sz listener_index);

SW_API i32 sw_server_listen(const c8* url, sw_http_handler handler, void* user_data);

SW_API i32 sw_http_reply(sw_connection* connection, i32 status_code, const c8* content_type, const void* body, sz body_len);
SW_API i32 sw_http_replyf(sw_connection* connection, i32 status_code, const c8* content_type, const c8* fmt, ...);
SW_API i32 sw_http_write(sw_connection* connection, const void* data, sz data_len);
SW_API i32 sw_http_printf(sw_connection* connection, const c8* fmt, ...);
SW_API i32 sw_http_serve_file(sw_connection* connection, const c8* path);

SW_API const c8* sw_http_header_get(const sw_http_message* hm, const c8* name);
SW_API i32 sw_http_get_var(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len);
SW_API i32 sw_http_next_multipart(const sw_http_message* hm, sw_http_multipart* mp, sz* offset);
SW_API void sw_http_multipart_clear(sw_http_multipart* mp);

SW_API const c8* sw_connection_remote_ip(const sw_connection* connection);
SW_API u16 sw_connection_remote_port(const sw_connection* connection);
SW_API void* sw_connection_user_data(sw_connection* connection);
SW_API void sw_connection_set_user_data(sw_connection* connection, void* user_data);

#ifdef __cplusplus
}
#endif

#endif
