#ifndef SW_SERVER_H
#define SW_SERVER_H

#include "sw_export.h"
#include "syphax/s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sw_mgr sw_mgr;
typedef struct sw_mgr sw_server;
typedef struct sw_connection sw_connection;
typedef struct sw_sessions sw_sessions;
typedef struct sw_session sw_session;
typedef struct sw_tokens sw_tokens;
typedef struct sw_token sw_token;

typedef enum sw_cookie_same_site {
    SW_COOKIE_SAMESITE_UNSET,
    SW_COOKIE_SAMESITE_LAX,
    SW_COOKIE_SAMESITE_STRICT,
    SW_COOKIE_SAMESITE_NONE
} sw_cookie_same_site;

typedef struct {
    const c8* name;
    const c8* value;
} sw_http_header;

typedef struct {
    const c8* path;
    const c8* domain;
    const c8* expires;
    i64 max_age;
    b8 http_only;
    b8 secure;
    b8 secure_auto;
    sw_cookie_same_site same_site;
} sw_http_cookie;

typedef struct {
    const c8* cookie_name;
    i32 ttl_seconds;
    sz max_sessions;
    sz max_items;
    sw_http_cookie cookie;
} sw_session_config;

typedef struct {
    const c8* cookie_name;
    const u8* secret;
    sz secret_len;
    i32 ttl_seconds;
    sz max_tokens;
    sz max_items;
    sw_http_cookie cookie;
} sw_token_config;

typedef struct {
    const c8* method;
    const c8* uri;
    const c8* proto;
    sw_http_header* headers;
    sz num_headers;
    const c8* body;
    sz body_len;
    sz content_length;
    b8 body_pending;
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

typedef struct {
    const c8* name;
    c8* value;
    sz value_len;
} sw_http_upload_field;

typedef i32 (*sw_http_upload_path_fn)(const sw_http_multipart* part, c8* path, sz path_len, void* user_data);

typedef struct sw_server_config {
    i32 listen_backlog;
    sz event_batch_size;
    sz worker_count;
    sz max_connections;
    sz max_header_bytes;
    sz max_body_bytes;
    sz max_header_count;
    sz max_read_buffer_bytes;
    sz max_write_buffer_bytes;
    sz initial_read_buffer_bytes;
    sz initial_write_buffer_bytes;
    sz keep_alive_max_requests;
    i32 idle_timeout_ms;
    i32 header_timeout_ms;
    i32 body_timeout_ms;
    i32 write_timeout_ms;
} sw_server_config;

typedef sw_server_config sw_http_config;

typedef struct {
    const c8* certificate_file;
    const c8* private_key_file;
    const c8* ca_file;
    const c8* ca_path;
    const c8* cipher_list;
    const c8* ciphersuites;
    b8 verify_client;
    b8 require_client_cert;
    i32 handshake_timeout_ms;
} sw_tls_config;

typedef void (*sw_http_handler)(sw_connection* connection, const sw_http_message* request, void* user_data);

SW_API sw_server_config sw_server_config_default(void);
SW_API sw_http_config sw_http_config_default(void);
SW_API sw_tls_config sw_tls_config_default(void);
SW_API sw_server* sw_server_create(const sw_server_config* config);
SW_API void sw_server_destroy(sw_server* server);
SW_API i32 sw_server_add_http(sw_server* server, const c8* url, sw_http_handler handler, void* user_data);
SW_API i32 sw_server_add_https(sw_server* server, const c8* url, const sw_tls_config* tls, sw_http_handler handler, void* user_data);
SW_API i32 sw_server_poll(sw_server* server, i32 timeout_ms);
SW_API i32 sw_server_run(sw_server* server);
SW_API i32 sw_server_start(sw_server* server);
SW_API i32 sw_server_wait(sw_server* server);
SW_API void sw_server_stop(sw_server* server);
SW_API b8 sw_server_is_running(const sw_server* server);
SW_API u16 sw_server_get_listener_port(const sw_server* server, sz listener_index);
SW_API sz sw_server_open_connections(const sw_server* server);
SW_API sz sw_server_worker_count(const sw_server* server);
SW_API sz sw_server_worker_open_connections(const sw_server* server, sz worker_index);
SW_API sz sw_server_worker_accepted_connections(const sw_server* server, sz worker_index);
SW_API sw_mgr* sw_mgr_create(const sw_http_config* config);
SW_API void sw_mgr_destroy(sw_mgr* mgr);
SW_API i32 sw_http_listen(sw_mgr* mgr, const c8* url, sw_http_handler handler, void* user_data);
SW_API i32 sw_http_listen_tls(sw_mgr* mgr, const c8* url, const sw_tls_config* tls, sw_http_handler handler, void* user_data);
SW_API i32 sw_mgr_poll(sw_mgr* mgr, i32 timeout_ms);
SW_API void sw_mgr_request_stop(sw_mgr* mgr);
SW_API b8 sw_mgr_is_running(const sw_mgr* mgr);
SW_API u16 sw_mgr_get_listener_port(const sw_mgr* mgr, sz listener_index);

SW_API i32 sw_server_listen(const c8* url, const sw_http_config* config, sw_http_handler handler, void* user_data);
SW_API i32 sw_server_listen_tls(const c8* url, const sw_http_config* config, const sw_tls_config* tls, sw_http_handler handler, void* user_data);

SW_API i32 sw_http_reply(sw_connection* connection, i32 status_code, const c8* content_type, const void* body, sz body_len);
SW_API i32 sw_http_replyf(sw_connection* connection, i32 status_code, const c8* content_type, const c8* fmt, ...);
SW_API i32 sw_http_set_header(sw_connection* connection, const c8* name, const c8* value);
SW_API i32 sw_http_redirect(sw_connection* connection, const c8* location);
SW_API i32 sw_http_write(sw_connection* connection, const void* data, sz data_len);
SW_API i32 sw_http_printf(sw_connection* connection, const c8* fmt, ...);
SW_API i32 sw_http_serve_path(sw_connection* connection, const c8* docroot, const c8* request_path);
SW_API sw_http_cookie sw_http_cookie_default(void);
SW_API i32 sw_http_set_cookie(sw_connection* connection, const c8* name, const c8* value, const sw_http_cookie* options);
SW_API i32 sw_http_clear_cookie(sw_connection* connection, const c8* name, const sw_http_cookie* options);

SW_API b8 sw_http_is(const sw_http_message* hm, const c8* method, const c8* path);
SW_API b8 sw_http_path_is(const sw_http_message* hm, const c8* path);
SW_API b8 sw_http_path_starts(const sw_http_message* hm, const c8* prefix);
SW_API const c8* sw_http_header_get(const sw_http_message* hm, const c8* name);
SW_API i32 sw_http_get_query(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len);
SW_API i32 sw_http_get_form(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len);
SW_API i32 sw_http_get_cookie(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len);
SW_API i32 sw_http_next_multipart(const sw_http_message* hm, sw_http_multipart* mp, sz* offset);
SW_API i32 sw_http_multipart_save(const sw_http_multipart* mp, const c8* path);
SW_API i32 sw_http_upload_save(sw_connection* connection, const sw_http_message* hm, const c8* name, const c8* path, sz* out_size);
SW_API i32 sw_http_upload_save_fields(
    sw_connection* connection,
    const sw_http_message* hm,
    const c8* file_field,
    sw_http_upload_path_fn path_fn,
    sw_http_upload_field* fields,
    sz field_count,
    sz* out_size,
    void* user_data
);
SW_API void sw_http_multipart_clear(sw_http_multipart* mp);

SW_API sw_session_config sw_session_config_default(void);
SW_API sw_sessions* sw_sessions_create(const sw_session_config* config);
SW_API void sw_sessions_destroy(sw_sessions* sessions);
SW_API sw_session* sw_sessions_find(sw_sessions* sessions, const sw_http_message* hm);
SW_API sw_session* sw_sessions_start(sw_sessions* sessions, sw_connection* connection, const sw_http_message* hm);
SW_API i32 sw_sessions_end(sw_sessions* sessions, sw_connection* connection, const sw_http_message* hm);
SW_API const c8* sw_session_id(const sw_session* session);
SW_API const c8* sw_session_get(const sw_session* session, const c8* key);
SW_API i32 sw_session_set(sw_session* session, const c8* key, const c8* value);
SW_API i32 sw_session_remove(sw_session* session, const c8* key);

SW_API sw_token_config sw_token_config_default(void);
SW_API sw_tokens* sw_tokens_create(const sw_token_config* config);
SW_API void sw_tokens_destroy(sw_tokens* tokens);
SW_API sw_token* sw_tokens_login(sw_tokens* tokens, sw_connection* connection, const sw_http_message* hm, const c8* user_id);
SW_API sw_token* sw_tokens_current(sw_tokens* tokens, sw_connection* connection, const sw_http_message* hm);
SW_API i32 sw_tokens_logout(sw_tokens* tokens, sw_connection* connection, const sw_http_message* hm);
SW_API const c8* sw_token_id(const sw_token* token);
SW_API const c8* sw_token_get(const sw_token* token, const c8* key);
SW_API i32 sw_token_set(sw_token* token, const c8* key, const c8* value);
SW_API i32 sw_token_remove(sw_token* token, const c8* key);

SW_API const c8* sw_connection_remote_ip(const sw_connection* connection);
SW_API u16 sw_connection_remote_port(const sw_connection* connection);
SW_API b8 sw_connection_is_secure(const sw_connection* connection);
SW_API const c8* sw_connection_alpn(const sw_connection* connection);
SW_API void* sw_connection_user_data(sw_connection* connection);
SW_API void sw_connection_set_user_data(sw_connection* connection, void* user_data);

#ifdef __cplusplus
}
#endif

#endif
