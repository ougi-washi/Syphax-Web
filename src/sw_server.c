#include "sw_backend.h"
#include "sw_utility.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#    include <bcrypt.h>
#endif

#if !defined(_WIN32)
#    include <signal.h>
#endif

#if defined(SYPHAX_WEB_HAS_TLS)
#    include <openssl/ssl.h>
#endif

#if defined(SYPHAX_WEB_HAS_CRYPTO)
#    include <openssl/evp.h>
#endif

typedef enum {
    SW_PARSE_PENDING = 0,
    SW_PARSE_READY = 1,
    SW_PARSE_BAD_REQUEST = -1,
    SW_PARSE_HEADERS_TOO_LARGE = -2,
    SW_PARSE_PAYLOAD_TOO_LARGE = -3,
    SW_PARSE_TOO_MANY_HEADERS = -4
} sw_parse_result;

#if defined(SYPHAX_WEB_HAS_TLS)
typedef enum {
    SW_TLS_OP_HANDSHAKE = 0,
    SW_TLS_OP_READ = 1,
    SW_TLS_OP_WRITE = 2
} sw_tls_operation;
#endif

typedef struct {
    c8* key;
    c8* value;
} sw_session_item;

typedef s_array(sw_session_item, sw_session_item_array);

struct sw_session {
    c8 id[65];
    sw_session_item_array items;
    f64 expires_at_ms;
    f64 touched_at_ms;
    sz max_items;
};

typedef s_array(sw_session, sw_session_array);

struct sw_sessions {
    sw_session_array sessions;
    c8* cookie_name;
    sw_http_cookie cookie;
    i32 ttl_seconds;
    sz max_sessions;
    sz max_items;
};

typedef struct {
    c8* key;
    c8* value;
} sw_token_item;

typedef s_array(sw_token_item, sw_token_item_array);

struct sw_token {
    c8 id[65];
    sw_token_item_array items;
    f64 expires_at_ms;
    f64 touched_at_ms;
    sz max_items;
};

typedef s_array(sw_token, sw_token_array);

struct sw_tokens {
    sw_token_array tokens;
    c8* cookie_name;
    sw_http_cookie cookie;
    i32 ttl_seconds;
    sz max_tokens;
    sz max_items;
    u8 secret[32];
};

static sz sw_socket_runtime_users = 0;
static sw_mgr* sw_signal_mgr = NULL;

static void sw_http_header_array_free_owned(sw_http_header_array* headers);
static b8 sw_parse_content_length_value(const c8* value, sz value_len, sz* out_length);
static b8 sw_ascii_case_equal_range(const c8* lhs, const c8* rhs, sz len);
static b8 sw_parse_listen_url_scheme(
    const c8* url,
    const c8* scheme,
    b8 require_scheme,
    const c8* default_port,
    c8* host,
    sz host_cap,
    c8* port,
    sz port_cap
);
static sz sw_find_bytes_from(const c8* data, sz data_len, sz offset, const c8* needle, sz needle_len);
static sz sw_find_crlf_from(const c8* data, sz data_len, sz offset);
static void sw_connection_mark_activity(sw_connection* connection, f64 now_ms);
static int sw_connection_shutdown_send(sw_mgr* mgr, sw_connection* connection);
static int sw_connection_discard_available_plain_input(sw_mgr* mgr, sw_connection* connection);
static i32 sw_server_run_loop(sw_mgr* mgr);
static i32 sw_mgr_effective_poll_timeout(sw_mgr* mgr, i32 timeout_ms);
static void sw_mgr_expire_timeouts(sw_mgr* mgr);
static void sw_mgr_schedule_connection_timeout(sw_mgr* mgr, sw_connection* connection, f64 now_ms);
static i32 sw_http_reply_status_text(sw_connection* connection, i32 status_code, const c8* body);
static i32 sw_http_send_file(sw_connection* connection, const c8* path);
static void sw_connection_clear_response_headers(sw_connection* connection);
static b8 sw_path_join(char* out, sz out_cap, const c8* lhs, const c8* rhs);
static b8 sw_path_real(const c8* path, char* out, sz out_cap);
static b8 sw_path_has_prefix(const c8* path, const c8* prefix);
static b8 sw_decode_request_path(const c8* request_path, char* out, sz out_cap);
static sz sw_http_path_length(const c8* uri);
static int sw_hex_value(c8 ch);
static b8 sw_http_content_type_matches(const c8* content_type, const c8* expected);
static b8 sw_http_content_type_boundary(const c8* content_type, char* boundary, sz boundary_cap);
static const c8* sw_find_multipart_boundary(
    const c8* data,
    sz data_len,
    sz offset,
    const c8* boundary_marker,
    sz boundary_marker_len,
    b8 require_crlf_prefix
);
static b8 sw_header_value_matches_name(const c8* line, sz line_len, const c8* name);
static c8* sw_parse_disposition_param(const c8* line, sz line_len, const c8* key);
static c8* sw_strdup_trimmed_range(const c8* text, sz text_len);
static i32 sw_random_bytes(u8* out, sz out_len);
static void sw_session_free(sw_session* session);
static void sw_sessions_remove_at(sw_sessions* sessions, sz index);
static void sw_token_free(sw_token* token);
#if defined(SYPHAX_WEB_HAS_CRYPTO)
static void sw_tokens_remove_at(sw_tokens* tokens, sz index);
#endif
static sw_mgr* sw_mgr_create_internal(const sw_server_config* config, b8 server_owner, sw_mgr* root, sw_worker* worker_owner);
static void sw_mgr_destroy_internal(sw_mgr* mgr);
static b8 sw_mgr_try_reserve_connection(sw_mgr* mgr);
static void sw_mgr_release_connection(sw_mgr* mgr);
static sz sw_mgr_active_connections(const sw_mgr* mgr);
static int sw_mgr_dispatch_connection(
    sw_mgr* mgr,
    sw_listener* listener,
    sw_socket fd,
    const c8* remote_ip,
    u16 remote_port,
    b8 counted
);
static int sw_mgr_drain_pending_connections(sw_mgr* mgr);
static void* sw_worker_thread_main(void* arg);
static void* sw_accept_thread_main(void* arg);
static i32 sw_server_start_workers(sw_mgr* server);
static void sw_server_join_workers(sw_mgr* server);
static void sw_mgr_close_all_connections(sw_mgr* mgr);
static b8 sw_request_allows_keep_alive(const sw_connection* connection);
static void sw_connection_prepare_response(sw_connection* connection);
static b8 sw_connection_append_output(sw_connection* connection, const void* data, sz data_len);
static int sw_connection_finish_response(sw_mgr* mgr, sw_connection* connection);
static int sw_connection_process_input(sw_mgr* mgr, sw_connection* connection);
static int sw_connection_dispatch_request(sw_mgr* mgr, sw_connection* connection);
static b8 sw_request_body_should_stream(const sw_http_message* request, const sw_server_config* config, sz header_bytes);
static sw_parse_result sw_connection_begin_streamed_body(
    sw_connection* connection,
    sw_http_message* parsed_request,
    sw_http_header_array* parsed_headers,
    sz header_bytes
);

#if defined(SYPHAX_WEB_HAS_TLS)
static SSL_CTX* sw_tls_context_create(const sw_tls_config* config);
static void sw_tls_context_free(SSL_CTX* ctx);
static int sw_connection_tls_handshake(sw_mgr* mgr, sw_connection* connection);
static b8 sw_connection_tls_handshake_pending(const sw_connection* connection);
static int sw_connection_transport_recv(sw_connection* connection, c8* data, sz data_cap, int* out_read);
static int sw_connection_transport_send(sw_connection* connection, const c8* data, sz data_len, int* out_sent);
#endif

#if defined(SYPHAX_WEB_HAS_CRYPTO)
static c8* sw_base64url_encode(const u8* data, sz data_len);
static u8* sw_base64url_decode(const c8* text, sz text_len, sz* out_len);
static b8 sw_token_encrypt_cookie_value(const sw_tokens* tokens, const c8 id[65], c8** out_value);
static b8 sw_token_decrypt_cookie_value(const sw_tokens* tokens, const c8* value, c8 out_id[65]);
#endif

#ifdef _WIN32
static BOOL WINAPI sw_console_handler(DWORD signal_type) {
    switch (signal_type) {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (sw_signal_mgr != NULL) {
                sw_mgr_request_stop(sw_signal_mgr);
            }
            return TRUE;
        default:
            return FALSE;
    }
}
#else
static void sw_signal_handler(int signal_number) {
    (void)signal_number;
    if (sw_signal_mgr != NULL) {
        sw_mgr_request_stop(sw_signal_mgr);
    }
}
#endif

int sw_socket_runtime_acquire(void) {
#ifdef _WIN32
    if (sw_socket_runtime_users == 0) {
        WSADATA wsa_data;
        if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
            return -1;
        }
    }
#endif
    ++sw_socket_runtime_users;
    return 0;
}

void sw_socket_runtime_release(void) {
    if (sw_socket_runtime_users == 0) {
        return;
    }
    --sw_socket_runtime_users;
#ifdef _WIN32
    if (sw_socket_runtime_users == 0) {
        WSACleanup();
    }
#endif
}

int sw_socket_set_nonblocking(sw_socket fd) {
#ifdef _WIN32
    u_long mode = 1;
    return ioctlsocket(fd, FIONBIO, &mode);
#else
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
}

int sw_socket_close(sw_socket fd) {
#ifdef _WIN32
    return closesocket(fd);
#else
    return close(fd);
#endif
}

int sw_socket_last_error(void) {
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

b8 sw_socket_error_is_would_block(int err) {
#ifdef _WIN32
    return err == WSAEWOULDBLOCK;
#else
    return err == EAGAIN || err == EWOULDBLOCK;
#endif
}

const c8* sw_http_status_text(i32 status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 400: return "Bad Request";
        case 408: return "Request Timeout";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 431: return "Request Header Fields Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

#if defined(SYPHAX_WEB_HAS_TLS)
static int sw_tls_alpn_select(
    SSL* ssl,
    const unsigned char** out,
    unsigned char* out_len,
    const unsigned char* in,
    unsigned int in_len,
    void* arg
) {
    static const unsigned char http11[] = { 8, 'h', 't', 't', 'p', '/', '1', '.', '1' };
    unsigned char* selected = NULL;
    unsigned char selected_len = 0;

    (void)ssl;
    (void)arg;
    if (SSL_select_next_proto(&selected, &selected_len, http11, sizeof(http11), in, in_len) == OPENSSL_NPN_NEGOTIATED) {
        *out = selected;
        *out_len = selected_len;
        return SSL_TLSEXT_ERR_OK;
    }
    return SSL_TLSEXT_ERR_NOACK;
}

static SSL_CTX* sw_tls_context_create(const sw_tls_config* config) {
    SSL_CTX* ctx;
    long options;
    int verify_mode = SSL_VERIFY_NONE;

    if (config == NULL || config->certificate_file == NULL || config->private_key_file == NULL) {
        return NULL;
    }

    if (OPENSSL_init_ssl(0, NULL) != 1) {
        return NULL;
    }

    ctx = SSL_CTX_new(TLS_server_method());
    if (ctx == NULL) {
        return NULL;
    }

    if (!SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION)) {
        SSL_CTX_free(ctx);
        return NULL;
    }

    options = SSL_OP_NO_SSLv2 | SSL_OP_NO_SSLv3 | SSL_OP_NO_TLSv1 | SSL_OP_NO_TLSv1_1
        | SSL_OP_NO_COMPRESSION;
#if defined(SSL_OP_NO_RENEGOTIATION)
    options |= SSL_OP_NO_RENEGOTIATION;
#endif
    SSL_CTX_set_options(ctx, options);

    if (config->cipher_list != NULL && SSL_CTX_set_cipher_list(ctx, config->cipher_list) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
#if defined(TLS1_3_VERSION)
    if (config->ciphersuites != NULL && SSL_CTX_set_ciphersuites(ctx, config->ciphersuites) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
#endif

    if (SSL_CTX_use_certificate_chain_file(ctx, config->certificate_file) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (SSL_CTX_use_PrivateKey_file(ctx, config->private_key_file, SSL_FILETYPE_PEM) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }
    if (SSL_CTX_check_private_key(ctx) != 1) {
        SSL_CTX_free(ctx);
        return NULL;
    }

    if (config->verify_client || config->require_client_cert) {
        if (config->ca_file != NULL || config->ca_path != NULL) {
            if (SSL_CTX_load_verify_locations(ctx, config->ca_file, config->ca_path) != 1) {
                SSL_CTX_free(ctx);
                return NULL;
            }
        } else if (SSL_CTX_set_default_verify_paths(ctx) != 1) {
            SSL_CTX_free(ctx);
            return NULL;
        }
        verify_mode = SSL_VERIFY_PEER;
        if (config->require_client_cert) {
            verify_mode |= SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
        }
    }
    SSL_CTX_set_verify(ctx, verify_mode, NULL);
    SSL_CTX_set_alpn_select_cb(ctx, sw_tls_alpn_select, NULL);
    return ctx;
}

static void sw_tls_context_free(SSL_CTX* ctx) {
    if (ctx != NULL) {
        SSL_CTX_free(ctx);
    }
}

static b8 sw_connection_tls_handshake_pending(const sw_connection* connection) {
    return connection != NULL && connection->tls != NULL && !connection->tls_handshake_complete;
}

static void sw_connection_tls_store_alpn(sw_connection* connection) {
    const unsigned char* selected = NULL;
    unsigned int selected_len = 0;
    sz copy_len;

    if (connection == NULL || connection->tls == NULL) {
        return;
    }

    SSL_get0_alpn_selected(connection->tls, &selected, &selected_len);
    if (selected == NULL || selected_len == 0) {
        connection->alpn[0] = '\0';
        return;
    }

    copy_len = selected_len < sizeof(connection->alpn) - 1 ? selected_len : sizeof(connection->alpn) - 1;
    memcpy(connection->alpn, selected, copy_len);
    connection->alpn[copy_len] = '\0';
}

static int sw_connection_tls_would_block(
    sw_connection* connection,
    SSL* ssl,
    int rc,
    sw_tls_operation operation,
    int* out_err
) {
    const int err = SSL_get_error(ssl, rc);

    if (out_err != NULL) {
        *out_err = err;
    }
    connection->tls_want_read = 0;
    connection->tls_want_write = 0;
    if (operation == SW_TLS_OP_READ) {
        connection->tls_read_wants_write = 0;
    } else if (operation == SW_TLS_OP_WRITE) {
        connection->tls_write_wants_read = 0;
    }
    if (err == SSL_ERROR_WANT_READ) {
        connection->tls_want_read = 1;
        if (operation == SW_TLS_OP_WRITE) {
            connection->tls_write_wants_read = 1;
        }
        return 1;
    }
    if (err == SSL_ERROR_WANT_WRITE) {
        connection->tls_want_write = 1;
        if (operation == SW_TLS_OP_READ) {
            connection->tls_read_wants_write = 1;
        }
        return 1;
    }
    return 0;
}

static int sw_connection_tls_handshake(sw_mgr* mgr, sw_connection* connection) {
    int rc;

    if (!sw_connection_tls_handshake_pending(connection)) {
        return 1;
    }

    rc = SSL_accept(connection->tls);
    if (rc == 1) {
        connection->tls_handshake_complete = 1;
        connection->tls_want_read = 0;
        connection->tls_want_write = 0;
        connection->tls_read_wants_write = 0;
        connection->tls_write_wants_read = 0;
        sw_connection_tls_store_alpn(connection);
        sw_connection_mark_activity(connection, sw_now_ms());
        return 1;
    }

    if (sw_connection_tls_would_block(connection, connection->tls, rc, SW_TLS_OP_HANDSHAKE, NULL)) {
        sw_mgr_sync_connection(mgr, connection);
        return 0;
    }

    return -1;
}

static int sw_connection_transport_recv(sw_connection* connection, c8* data, sz data_cap, int* out_read) {
    if (out_read != NULL) {
        *out_read = 0;
    }
    if (connection == NULL || data == NULL || data_cap == 0 || out_read == NULL) {
        return -1;
    }

    if (connection->tls != NULL) {
        const int cap = data_cap > (sz)INT_MAX ? INT_MAX : (int)data_cap;
        const int rc = SSL_read(connection->tls, data, cap);
        int ssl_err = SSL_ERROR_NONE;
        if (rc > 0) {
            connection->tls_want_read = 0;
            connection->tls_want_write = 0;
            connection->tls_read_wants_write = 0;
            *out_read = rc;
            return 1;
        }
        if (sw_connection_tls_would_block(connection, connection->tls, rc, SW_TLS_OP_READ, &ssl_err)) {
            return 0;
        }
        if (ssl_err == SSL_ERROR_ZERO_RETURN) {
            return -2;
        }
        return -1;
    }

    {
        const int rc = (int)recv(connection->fd, data, (int)data_cap, 0);
        if (rc > 0) {
            *out_read = rc;
            return 1;
        }
        if (rc == 0) {
            return -2;
        }
        if (sw_socket_error_is_would_block(sw_socket_last_error())) {
            return 0;
        }
        return -1;
    }
}

static int sw_connection_transport_send(sw_connection* connection, const c8* data, sz data_len, int* out_sent) {
    if (out_sent != NULL) {
        *out_sent = 0;
    }
    if (connection == NULL || data == NULL || data_len == 0 || out_sent == NULL) {
        return -1;
    }

    if (connection->tls != NULL) {
        const int to_send = data_len > (sz)INT_MAX ? INT_MAX : (int)data_len;
        const int rc = SSL_write(connection->tls, data, to_send);
        if (rc > 0) {
            connection->tls_want_read = 0;
            connection->tls_want_write = 0;
            connection->tls_write_wants_read = 0;
            *out_sent = rc;
            return 1;
        }
        if (sw_connection_tls_would_block(connection, connection->tls, rc, SW_TLS_OP_WRITE, NULL)) {
            return 0;
        }
        return -1;
    }

    {
        const sz capped_len = data_len > (sz)INT_MAX ? (sz)INT_MAX : data_len;
        const int rc = (int)send(connection->fd, data, (int)capped_len, 0);
        if (rc > 0) {
            *out_sent = rc;
            return 1;
        }
        if (sw_socket_error_is_would_block(sw_socket_last_error())) {
            return 0;
        }
        return -1;
    }
}
#endif

static sz sw_find_bytes(const c8* data, sz data_len, const c8* needle, sz needle_len) {
    sz i;

    if (needle_len == 0 || data_len < needle_len) {
        return SIZE_MAX;
    }

    for (i = 0; i + needle_len <= data_len; ++i) {
        if (memcmp(data + i, needle, needle_len) == 0) {
            return i;
        }
    }

    return SIZE_MAX;
}

static sz sw_find_bytes_from(const c8* data, sz data_len, sz offset, const c8* needle, sz needle_len) {
    const sz rel = (offset <= data_len) ? sw_find_bytes(data + offset, data_len - offset, needle, needle_len) : SIZE_MAX;
    return rel == SIZE_MAX ? SIZE_MAX : offset + rel;
}

static sz sw_find_crlf_from(const c8* data, sz data_len, sz offset) {
    return sw_find_bytes_from(data, data_len, offset, "\r\n", 2);
}

static void sw_http_header_array_free_owned(sw_http_header_array* headers) {
    sz i;
    sw_http_header* data;

    if (headers == NULL) {
        return;
    }

    data = s_array_get_data(headers);
    for (i = 0; i < s_array_get_size(headers); ++i) {
        free((void*)data[i].name);
        free((void*)data[i].value);
    }
    s_array_clear(headers);
}

static void sw_connection_clear_response_headers(sw_connection* connection) {
    if (connection != NULL) {
        sw_http_header_array_free_owned(&connection->response_headers);
        s_array_init(&connection->response_headers);
    }
}

static b8 sw_parse_content_length_value(const c8* value, sz value_len, sz* out_length) {
    sz length = 0;
    sz i = 0;
    sz end = value_len;

    if (out_length != NULL) {
        *out_length = 0;
    }
    if (value == NULL || out_length == NULL) {
        return 0;
    }

    while (i < value_len && isspace((unsigned char)value[i])) {
        ++i;
    }
    while (end > i && isspace((unsigned char)value[end - 1])) {
        --end;
    }
    if (i == end) {
        return 0;
    }

    for (; i < end; ++i) {
        const unsigned char ch = (unsigned char)value[i];
        if (ch < '0' || ch > '9') {
            return 0;
        }
        if (length > (SIZE_MAX - (sz)(ch - '0')) / 10) {
            return 0;
        }
        length = (length * 10) + (sz)(ch - '0');
    }

    *out_length = length;
    return 1;
}

static b8 sw_ascii_case_equal_range(const c8* lhs, const c8* rhs, sz len) {
    sz i;

    if (lhs == NULL || rhs == NULL) {
        return 0;
    }
    for (i = 0; i < len; ++i) {
        if (tolower((unsigned char)lhs[i]) != tolower((unsigned char)rhs[i])) {
            return 0;
        }
    }
    return 1;
}

static b8 sw_http_header_value_has_token(const c8* value, const c8* token) {
    const sz token_len = token != NULL ? strlen(token) : 0;
    sz cursor = 0;

    if (value == NULL || token == NULL || token_len == 0) {
        return 0;
    }

    while (value[cursor] != '\0') {
        sz begin;
        sz end;

        while (value[cursor] == ',' || isspace((unsigned char)value[cursor])) {
            ++cursor;
        }
        begin = cursor;
        while (value[cursor] != '\0' && value[cursor] != ',') {
            ++cursor;
        }
        end = cursor;
        while (end > begin && isspace((unsigned char)value[end - 1])) {
            --end;
        }

        if (end > begin
            && end - begin == token_len
            && sw_ascii_case_equal_range(value + begin, token, token_len)) {
            return 1;
        }
    }

    return 0;
}

static int sw_hex_digit_value(c8 ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'f') {
        return 10 + (ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F') {
        return 10 + (ch - 'A');
    }
    return -1;
}

static b8 sw_parse_chunk_size_value(const c8* value, sz value_len, sz* out_size) {
    sz size = 0;
    sz i = 0;
    b8 saw_digit = 0;

    if (out_size != NULL) {
        *out_size = 0;
    }
    if (value == NULL || out_size == NULL) {
        return 0;
    }

    while (i < value_len) {
        const int digit = sw_hex_digit_value(value[i]);
        if (digit < 0) {
            break;
        }
        if (size > (SIZE_MAX - (sz)digit) / 16) {
            return 0;
        }
        size = (size * 16) + (sz)digit;
        saw_digit = 1;
        ++i;
    }
    if (!saw_digit) {
        return 0;
    }

    while (i < value_len && (value[i] == ' ' || value[i] == '\t')) {
        ++i;
    }
    if (i < value_len && value[i] != ';') {
        return 0;
    }

    *out_size = size;
    return 1;
}

static sw_parse_result sw_decode_chunked_body(
    const c8* data,
    sz data_len,
    sz body_offset,
    sz max_body_bytes,
    sz max_trailer_bytes,
    c8** out_body,
    sz* out_body_len,
    sz* out_request_len
) {
    sw_char_array decoded;
    sz cursor = body_offset;

    if (out_body != NULL) {
        *out_body = NULL;
    }
    if (out_body_len != NULL) {
        *out_body_len = 0;
    }
    if (out_request_len != NULL) {
        *out_request_len = 0;
    }
    if (data == NULL || out_body == NULL || out_body_len == NULL || out_request_len == NULL || body_offset > data_len) {
        return SW_PARSE_BAD_REQUEST;
    }

    sw_char_array_init(&decoded);

    for (;;) {
        const sz line_end = sw_find_crlf_from(data, data_len, cursor);
        sz chunk_size;

        if (line_end == SIZE_MAX) {
            sw_char_array_free(&decoded);
            return SW_PARSE_PENDING;
        }
        if (!sw_parse_chunk_size_value(data + cursor, line_end - cursor, &chunk_size)) {
            sw_char_array_free(&decoded);
            return SW_PARSE_BAD_REQUEST;
        }
        cursor = line_end + 2;

        if (chunk_size == 0) {
            sz body_len;

            if (cursor + 2 <= data_len && data[cursor] == '\r' && data[cursor + 1] == '\n') {
                *out_request_len = cursor + 2;
            } else {
                const sz trailer_end = sw_find_bytes_from(data, data_len, cursor, "\r\n\r\n", 4);
                if (trailer_end == SIZE_MAX) {
                    if (data_len - cursor > max_trailer_bytes) {
                        sw_char_array_free(&decoded);
                        return SW_PARSE_HEADERS_TOO_LARGE;
                    }
                    sw_char_array_free(&decoded);
                    return SW_PARSE_PENDING;
                }
                if (trailer_end - cursor > max_trailer_bytes) {
                    sw_char_array_free(&decoded);
                    return SW_PARSE_HEADERS_TOO_LARGE;
                }
                *out_request_len = trailer_end + 4;
            }

            body_len = sw_char_array_size(&decoded);
            *out_body = sw_strdup_range(sw_char_array_data(&decoded), body_len);
            sw_char_array_free(&decoded);
            if (*out_body == NULL) {
                return SW_PARSE_BAD_REQUEST;
            }
            *out_body_len = body_len;
            return SW_PARSE_READY;
        }

        if (chunk_size > max_body_bytes - sw_char_array_size(&decoded)) {
            sw_char_array_free(&decoded);
            return SW_PARSE_PAYLOAD_TOO_LARGE;
        }
        if (chunk_size > data_len - cursor) {
            sw_char_array_free(&decoded);
            return SW_PARSE_PENDING;
        }
        if (data_len - cursor - chunk_size < 2) {
            sw_char_array_free(&decoded);
            return SW_PARSE_PENDING;
        }
        if (data[cursor + chunk_size] != '\r' || data[cursor + chunk_size + 1] != '\n') {
            sw_char_array_free(&decoded);
            return SW_PARSE_BAD_REQUEST;
        }
        if (!sw_char_array_append_bytes(&decoded, data + cursor, chunk_size)) {
            sw_char_array_free(&decoded);
            return SW_PARSE_BAD_REQUEST;
        }
        cursor += chunk_size + 2;
    }
}

static b8 sw_parse_listen_url_scheme(
    const c8* url,
    const c8* scheme,
    b8 require_scheme,
    const c8* default_port,
    c8* host,
    sz host_cap,
    c8* port,
    sz port_cap
) {
    const c8* cursor = url;
    const c8* authority_end;
    const c8* host_begin;
    const c8* host_end;
    const c8* port_begin = NULL;
    const c8* scheme_sep;
    const sz scheme_len = scheme != NULL ? strlen(scheme) : 0;
    sz host_len;
    sz port_len;

    if (url == NULL || scheme == NULL || default_port == NULL
        || host == NULL || port == NULL || host_cap == 0 || port_cap == 0) {
        return 0;
    }

    scheme_sep = strstr(cursor, "://");
    if (scheme_sep != NULL) {
        if ((sz)(scheme_sep - cursor) != scheme_len || strncmp(cursor, scheme, scheme_len) != 0) {
            return 0;
        }
        cursor = scheme_sep + 3;
    } else if (require_scheme) {
        return 0;
    }

    authority_end = strchr(cursor, '/');
    if (authority_end == NULL) {
        authority_end = cursor + strlen(cursor);
    }

    if (*cursor == '[') {
        const c8* close = strchr(cursor, ']');
        if (close == NULL || close > authority_end) {
            return 0;
        }
        host_begin = cursor + 1;
        host_end = close;
        if (close + 1 < authority_end && close[1] == ':') {
            port_begin = close + 2;
        }
    } else {
        const c8* colon = NULL;
        const c8* it;
        for (it = cursor; it < authority_end; ++it) {
            if (*it == ':') {
                colon = it;
            }
        }
        host_begin = cursor;
        host_end = (colon != NULL) ? colon : authority_end;
        if (colon != NULL) {
            port_begin = colon + 1;
        }
    }

    host_len = (sz)(host_end - host_begin);
    if (host_len >= host_cap) {
        return 0;
    }
    memcpy(host, host_begin, host_len);
    host[host_len] = '\0';

    if (port_begin == NULL || port_begin >= authority_end) {
        const int written = snprintf(port, port_cap, "%s", default_port);
        if (written < 0 || (sz)written >= port_cap) {
            return 0;
        }
        return 1;
    }

    port_len = (sz)(authority_end - port_begin);
    if (port_len == 0 || port_len >= port_cap) {
        return 0;
    }
    memcpy(port, port_begin, port_len);
    port[port_len] = '\0';
    return 1;
}

static b8 sw_parse_listen_url(const c8* url, c8* host, sz host_cap, c8* port, sz port_cap) {
    return sw_parse_listen_url_scheme(url, "http", 0, "80", host, host_cap, port, port_cap);
}

static sw_socket sw_create_listener_socket(const c8* host, const c8* port, i32 listen_backlog, u16* out_bound_port) {
    struct addrinfo hints;
    struct addrinfo* results = NULL;
    struct addrinfo* current;
    sw_socket listener_fd = SW_INVALID_SOCKET;
    int rc;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    rc = getaddrinfo((host != NULL && host[0] != '\0') ? host : NULL, port, &hints, &results);
    if (rc != 0) {
        return SW_INVALID_SOCKET;
    }

    for (current = results; current != NULL; current = current->ai_next) {
        int reuse = 1;
        listener_fd = (sw_socket)socket(current->ai_family, current->ai_socktype, current->ai_protocol);
        if (listener_fd == SW_INVALID_SOCKET) {
            continue;
        }

        setsockopt(listener_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

        if (bind(listener_fd, current->ai_addr, (socklen_t)current->ai_addrlen) == 0
            && listen(listener_fd, listen_backlog > 0 ? listen_backlog : 128) == 0
            && sw_socket_set_nonblocking(listener_fd) == 0) {
            if (out_bound_port != NULL) {
                struct sockaddr_storage bound_addr;
                socklen_t bound_len = (socklen_t)sizeof(bound_addr);
                if (getsockname(listener_fd, (struct sockaddr*)&bound_addr, &bound_len) == 0) {
                    if (bound_addr.ss_family == AF_INET) {
                        *out_bound_port = ntohs(((struct sockaddr_in*)&bound_addr)->sin_port);
                    } else if (bound_addr.ss_family == AF_INET6) {
                        *out_bound_port = ntohs(((struct sockaddr_in6*)&bound_addr)->sin6_port);
                    }
                }
            }
            freeaddrinfo(results);
            return listener_fd;
        }

        sw_socket_close(listener_fd);
        listener_fd = SW_INVALID_SOCKET;
    }

    freeaddrinfo(results);
    return SW_INVALID_SOCKET;
}

static sz sw_atomic_load_size(const sz* value) {
#if defined(__GNUC__) || defined(__clang__)
    return __atomic_load_n(value, __ATOMIC_RELAXED);
#else
    return *value;
#endif
}

static b8 sw_atomic_try_increment_size(sz* value, sz max_value) {
#if defined(__GNUC__) || defined(__clang__)
    sz current = __atomic_load_n(value, __ATOMIC_RELAXED);
    for (;;) {
        if (max_value > 0 && current >= max_value) {
            return 0;
        }
        if (__atomic_compare_exchange_n(value, &current, current + 1, 0, __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {
            return 1;
        }
    }
#else
    if (max_value > 0 && *value >= max_value) {
        return 0;
    }
    *value += 1;
    return 1;
#endif
}

static void sw_atomic_decrement_size(sz* value) {
#if defined(__GNUC__) || defined(__clang__)
    __atomic_fetch_sub(value, 1, __ATOMIC_RELAXED);
#else
    if (*value > 0) {
        *value -= 1;
    }
#endif
}

static sz sw_mgr_active_connections(const sw_mgr* mgr) {
    const sw_mgr* root = (mgr != NULL && mgr->root != NULL) ? mgr->root : mgr;
    return root != NULL ? sw_atomic_load_size(&root->active_connection_count) : 0;
}

static b8 sw_mgr_try_reserve_connection(sw_mgr* mgr) {
    sw_mgr* root = (mgr != NULL && mgr->root != NULL) ? mgr->root : mgr;

    if (root == NULL) {
        return 0;
    }
    return sw_atomic_try_increment_size(&root->active_connection_count, root->config.max_connections);
}

static void sw_mgr_release_connection(sw_mgr* mgr) {
    sw_mgr* root = (mgr != NULL && mgr->root != NULL) ? mgr->root : mgr;

    if (root != NULL && sw_atomic_load_size(&root->active_connection_count) > 0) {
        sw_atomic_decrement_size(&root->active_connection_count);
    }
}

static sw_connection* sw_connection_create(
    sw_mgr* mgr,
    sw_listener* listener,
    sw_socket fd,
    const c8* remote_ip,
    u16 remote_port,
    b8 counted
) {
    sw_connection* connection = (sw_connection*)calloc(1, sizeof(*connection));
    s_handle handle;
    const f64 now_ms = sw_now_ms();

    if (connection == NULL) {
        return NULL;
    }

    connection->source_kind = SW_SOURCE_CONNECTION;
    connection->fd = fd;
    connection->mgr = mgr;
    connection->listener = listener;
    connection->handler = listener->handler;
    connection->handler_user_data = listener->handler_user_data;
    connection->remote_port = remote_port;
    connection->counted = counted;
    connection->opened_at_ms = now_ms;
    connection->phase_started_at_ms = now_ms;
    connection->last_activity_at_ms = now_ms;
    if (remote_ip != NULL) {
        snprintf(connection->remote_ip, sizeof(connection->remote_ip), "%s", remote_ip);
    }

#if defined(SYPHAX_WEB_HAS_TLS)
    if (listener->tls_enabled) {
        connection->secure = 1;
        connection->tls_handshake_timeout_ms = listener->tls_handshake_timeout_ms;
        connection->tls_handshake_started_at_ms = now_ms;
        connection->tls_want_read = 1;
        connection->tls = SSL_new(listener->tls_ctx);
        if (connection->tls == NULL) {
            free(connection);
            return NULL;
        }
        SSL_set_fd(connection->tls, (int)fd);
        SSL_set_accept_state(connection->tls);
        SSL_set_mode(connection->tls, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
    }
#else
    connection->secure = listener->tls_enabled;
#endif

    sw_char_array_init(&connection->read_buffer);
    sw_char_array_init(&connection->write_buffer);
    if (mgr->config.initial_read_buffer_bytes > 0) {
        s_array_reserve(&connection->read_buffer, mgr->config.initial_read_buffer_bytes);
    }
    if (mgr->config.initial_write_buffer_bytes > 0) {
        s_array_reserve(&connection->write_buffer, mgr->config.initial_write_buffer_bytes);
    }
    s_array_init(&connection->header_storage);
    s_array_init(&connection->response_headers);

    handle = s_array_add(&mgr->connections, connection);
    connection->array_handle = handle;

    if (sw_backend_register_connection(mgr, connection) != 0) {
        s_array_remove(&mgr->connections, handle);
#if defined(SYPHAX_WEB_HAS_TLS)
        if (connection->tls != NULL) {
            SSL_free(connection->tls);
        }
#endif
        sw_char_array_free(&connection->read_buffer);
        sw_char_array_free(&connection->write_buffer);
        s_array_clear(&connection->header_storage);
        s_array_clear(&connection->response_headers);
        free(connection);
        return NULL;
    }

    sw_mgr_schedule_connection_timeout(mgr, connection, now_ms);
    return connection;
}

void sw_connection_reset_request(sw_connection* connection) {
    sz i;

    if (connection == NULL) {
        return;
    }

    free((void*)connection->request.method);
    free((void*)connection->request.uri);
    free((void*)connection->request.proto);
    free((void*)connection->request.body);

    for (i = 0; i < s_array_get_size(&connection->header_storage); ++i) {
        sw_http_header* header = &s_array_get_data(&connection->header_storage)[i];
        free((void*)header->name);
        free((void*)header->value);
    }

    s_array_clear(&connection->header_storage);
    memset(&connection->request, 0, sizeof(connection->request));
    connection->request_ready = 0;
    connection->request_dispatched = 0;
    connection->request_started = 0;
    connection->headers_complete = 0;
    connection->response_started = 0;
    connection->must_close = 0;
    connection->parsed_request_bytes = 0;
    connection->phase_started_at_ms = sw_now_ms();
    if (connection->mgr != NULL) {
        sw_mgr_schedule_connection_timeout(connection->mgr, connection, connection->phase_started_at_ms);
    }
}

static void sw_connection_mark_activity(sw_connection* connection, f64 now_ms) {
    if (connection != NULL) {
        connection->last_activity_at_ms = now_ms;
        if (connection->mgr != NULL) {
            sw_mgr_schedule_connection_timeout(connection->mgr, connection, now_ms);
        }
    }
}

static b8 sw_connection_timeout_due_ms(const sw_mgr* mgr, const sw_connection* connection, f64 now_ms, f64* out_due_ms) {
    f64 due_ms = 0.0;
    b8 has_timeout = 0;

    if (mgr == NULL || connection == NULL || out_due_ms == NULL) {
        return 0;
    }

    if (connection->write_shutdown) {
        if (mgr->config.idle_timeout_ms > 0) {
            *out_due_ms = connection->last_activity_at_ms + (f64)mgr->config.idle_timeout_ms;
            return 1;
        }
        return 0;
    }

#if defined(SYPHAX_WEB_HAS_TLS)
    if (sw_connection_tls_handshake_pending(connection) && connection->tls_handshake_timeout_ms > 0) {
        due_ms = connection->tls_handshake_started_at_ms + (f64)connection->tls_handshake_timeout_ms;
        has_timeout = 1;
    }
#endif

    if (sw_connection_has_pending_output(connection) && mgr->config.write_timeout_ms > 0) {
        const f64 write_due_ms = connection->last_activity_at_ms + (f64)mgr->config.write_timeout_ms;
        if (!has_timeout || write_due_ms < due_ms) {
            due_ms = write_due_ms;
        }
        has_timeout = 1;
    }

    if (connection->request_started && !connection->request_ready
#if defined(SYPHAX_WEB_HAS_TLS)
        && !sw_connection_tls_handshake_pending(connection)
#endif
    ) {
        const i32 phase_timeout_ms = connection->headers_complete ? mgr->config.body_timeout_ms : mgr->config.header_timeout_ms;
        if (phase_timeout_ms > 0) {
            const f64 phase_due_ms = connection->phase_started_at_ms + (f64)phase_timeout_ms;
            if (!has_timeout || phase_due_ms < due_ms) {
                due_ms = phase_due_ms;
            }
            has_timeout = 1;
        }
    }

    if (mgr->config.idle_timeout_ms > 0) {
        const f64 idle_due_ms = connection->last_activity_at_ms + (f64)mgr->config.idle_timeout_ms;
        if (!has_timeout || idle_due_ms < due_ms) {
            due_ms = idle_due_ms;
        }
        has_timeout = 1;
    }

    if (!has_timeout) {
        return 0;
    }
    (void)now_ms;
    *out_due_ms = due_ms;
    return 1;
}

static void sw_timer_heap_swap(sw_timer_entry* entries, sz lhs, sz rhs) {
    const sw_timer_entry tmp = entries[lhs];
    entries[lhs] = entries[rhs];
    entries[rhs] = tmp;
}

static void sw_timer_heap_push(sw_timer_array* timers, sw_timer_entry entry) {
    sz index;
    sw_timer_entry* entries;

    s_array_add(timers, entry);
    index = s_array_get_size(timers) - 1;
    entries = s_array_get_data(timers);
    while (index > 0) {
        const sz parent = (index - 1) / 2;
        if (entries[parent].due_ms <= entries[index].due_ms) {
            break;
        }
        sw_timer_heap_swap(entries, parent, index);
        index = parent;
    }
}

static b8 sw_timer_heap_pop(sw_timer_array* timers, sw_timer_entry* out_entry) {
    const sz size = s_array_get_size(timers);
    sw_timer_entry* entries;
    sz index = 0;

    if (size == 0) {
        return 0;
    }

    entries = s_array_get_data(timers);
    if (out_entry != NULL) {
        *out_entry = entries[0];
    }
    if (size == 1) {
        s_array_remove(timers, s_array_handle(timers, 0));
        return 1;
    }

    entries[0] = entries[size - 1];
    s_array_remove(timers, s_array_handle(timers, (u32)(size - 1)));
    entries = s_array_get_data(timers);

    for (;;) {
        const sz left = (index * 2) + 1;
        const sz right = left + 1;
        sz smallest = index;

        if (left < s_array_get_size(timers) && entries[left].due_ms < entries[smallest].due_ms) {
            smallest = left;
        }
        if (right < s_array_get_size(timers) && entries[right].due_ms < entries[smallest].due_ms) {
            smallest = right;
        }
        if (smallest == index) {
            break;
        }
        sw_timer_heap_swap(entries, index, smallest);
        index = smallest;
    }

    return 1;
}

static b8 sw_mgr_peek_timer(sw_mgr* mgr, f64 now_ms, sw_timer_entry* out_entry) {
    while (mgr != NULL && s_array_get_size(&mgr->timers) > 0) {
        sw_timer_entry entry = s_array_get_data(&mgr->timers)[0];
        sw_connection** connection_slot = s_array_get(&mgr->connections, entry.connection_handle);
        sw_connection* connection = connection_slot != NULL ? *connection_slot : NULL;
        f64 due_ms = 0.0;

        if (connection == NULL
            || connection->timer_generation != entry.generation
            || !sw_connection_timeout_due_ms(mgr, connection, now_ms, &due_ms)
            || due_ms != entry.due_ms) {
            sw_timer_heap_pop(&mgr->timers, NULL);
            continue;
        }
        if (out_entry != NULL) {
            *out_entry = entry;
        }
        return 1;
    }
    return 0;
}

static void sw_mgr_schedule_connection_timeout(sw_mgr* mgr, sw_connection* connection, f64 now_ms) {
    f64 due_ms;
    sw_timer_entry entry;

    if (mgr == NULL || connection == NULL) {
        return;
    }
    if (!sw_connection_timeout_due_ms(mgr, connection, now_ms, &due_ms)) {
        return;
    }

    connection->timer_generation += 1;
    entry.due_ms = due_ms;
    entry.connection_handle = connection->array_handle;
    entry.generation = connection->timer_generation;
    sw_timer_heap_push(&mgr->timers, entry);
}

void sw_mgr_close_connection(sw_mgr* mgr, sw_connection* connection) {
    if (mgr == NULL || connection == NULL) {
        return;
    }

    sw_backend_unregister_connection(mgr, connection);

#if defined(SYPHAX_WEB_HAS_TLS)
    if (connection->tls != NULL) {
        (void)SSL_shutdown(connection->tls);
        SSL_free(connection->tls);
        connection->tls = NULL;
    }
#endif

    if (connection->fd != SW_INVALID_SOCKET) {
        sw_socket_close(connection->fd);
        connection->fd = SW_INVALID_SOCKET;
    }

    if (connection->file_stream != NULL) {
        fclose(connection->file_stream);
        connection->file_stream = NULL;
    }

    sw_connection_reset_request(connection);
    sw_connection_clear_response_headers(connection);
    sw_char_array_free(&connection->read_buffer);
    sw_char_array_free(&connection->write_buffer);

    if (connection->array_handle != S_HANDLE_NULL) {
        s_array_remove(&mgr->connections, connection->array_handle);
    }

    if (connection->counted) {
        sw_mgr_release_connection(mgr);
        connection->counted = 0;
    }

    free(connection);
}

int sw_mgr_sync_connection(sw_mgr* mgr, sw_connection* connection) {
    if (mgr == NULL || connection == NULL) {
        return -1;
    }
    sw_mgr_schedule_connection_timeout(mgr, connection, sw_now_ms());
    return sw_backend_update_connection(mgr, connection);
}

b8 sw_connection_has_pending_output(const sw_connection* connection) {
    return connection != NULL
        && (sw_char_array_size(&connection->write_buffer) > 0 || connection->file_stream != NULL);
}

static int sw_connection_shutdown_send(sw_mgr* mgr, sw_connection* connection) {
    if (mgr == NULL || connection == NULL) {
        return -1;
    }
#if defined(SYPHAX_WEB_HAS_TLS)
    if (connection->tls != NULL) {
        sw_mgr_close_connection(mgr, connection);
        return -1;
    }
#endif
    if (!connection->write_shutdown) {
#ifdef _WIN32
        if (shutdown(connection->fd, SD_SEND) != 0) {
#else
        if (shutdown(connection->fd, SHUT_WR) != 0) {
#endif
            sw_mgr_close_connection(mgr, connection);
            return -1;
        }
        connection->write_shutdown = 1;
        connection->close_after_write = 0;
        sw_connection_mark_activity(connection, sw_now_ms());
    }
    return sw_mgr_sync_connection(mgr, connection);
}

static int sw_connection_discard_available_plain_input(sw_mgr* mgr, sw_connection* connection) {
    c8 chunk[4096];

    if (mgr == NULL || connection == NULL) {
        return -1;
    }
#if defined(SYPHAX_WEB_HAS_TLS)
    if (connection->tls != NULL) {
        return 0;
    }
#endif

    for (;;) {
        const int read_bytes = (int)recv(connection->fd, chunk, sizeof(chunk), 0);
        if (read_bytes > 0) {
            continue;
        }
        if (read_bytes == 0) {
            sw_mgr_close_connection(mgr, connection);
            return -1;
        }

        if (sw_socket_error_is_would_block(sw_socket_last_error())) {
            return 0;
        }

        sw_mgr_close_connection(mgr, connection);
        return -1;
    }
}

b8 sw_connection_wants_read(const sw_connection* connection) {
    if (connection == NULL) {
        return 0;
    }
#if defined(SYPHAX_WEB_HAS_TLS)
    if (connection->tls != NULL && !connection->tls_handshake_complete) {
        return connection->tls_want_read || !connection->tls_want_write;
    }
    if (connection->tls != NULL && connection->tls_write_wants_read) {
        return 1;
    }
#endif
    return 1;
}

b8 sw_connection_wants_write(const sw_connection* connection) {
    if (connection == NULL) {
        return 0;
    }
#if defined(SYPHAX_WEB_HAS_TLS)
    if (connection->tls != NULL && !connection->tls_handshake_complete) {
        return connection->tls_want_write;
    }
    if (connection->tls != NULL && connection->tls_read_wants_write) {
        return 1;
    }
    if (connection->tls != NULL && connection->tls_write_wants_read) {
        return 0;
    }
    if (connection->tls != NULL && connection->tls_want_write) {
        return 1;
    }
#endif
    return sw_connection_has_pending_output(connection);
}

static b8 sw_request_body_should_stream(const sw_http_message* request, const sw_server_config* config, sz header_bytes) {
    const sz default_stream_threshold = 1024 * 1024;
    sz memory_limit = default_stream_threshold;
    char boundary[256];
    const c8* content_type;

    if (request == NULL || config == NULL || request->content_length == 0) {
        return 0;
    }
    content_type = sw_http_header_get(request, "Content-Type");
    if (!sw_http_content_type_matches(content_type, "multipart/form-data")
        || !sw_http_content_type_boundary(content_type, boundary, sizeof(boundary))) {
        return 0;
    }

    if (config->max_read_buffer_bytes > 0) {
        memory_limit = config->max_read_buffer_bytes > header_bytes
            ? config->max_read_buffer_bytes - header_bytes
            : 0;
        if (request->content_length > memory_limit) {
            return 1;
        }
    }

    if (request->content_length > default_stream_threshold) {
        return 1;
    }

    return 0;
}

static sw_parse_result sw_connection_begin_streamed_body(
    sw_connection* connection,
    sw_http_message* parsed_request,
    sw_http_header_array* parsed_headers,
    sz header_bytes
) {
    if (connection == NULL || parsed_request == NULL || parsed_headers == NULL) {
        return SW_PARSE_BAD_REQUEST;
    }

    parsed_request->body = NULL;
    parsed_request->body_len = 0;
    parsed_request->body_pending = 1;
    parsed_request->headers = s_array_get_data(parsed_headers);
    parsed_request->num_headers = s_array_get_size(parsed_headers);

    connection->header_storage = *parsed_headers;
    s_array_init(parsed_headers);
    connection->request = *parsed_request;
    memset(parsed_request, 0, sizeof(*parsed_request));
    connection->request_ready = 1;
    connection->must_close = 1;
    connection->parsed_request_bytes = 0;
    sw_char_array_consume_prefix(&connection->read_buffer, header_bytes);
    return SW_PARSE_READY;
}

static sw_parse_result sw_connection_try_parse_request(sw_connection* connection) {
    const sw_server_config* config;
    const c8* data;
    const sz data_len = sw_char_array_size(&connection->read_buffer);
    const sz header_end = sw_find_bytes(sw_char_array_data(&connection->read_buffer), data_len, "\r\n\r\n", 4);
    sz first_line_end;
    sz cursor;
    sw_http_header_array parsed_headers;
    sw_http_message parsed_request;
    b8 saw_content_length = 0;

    if (connection->request_ready) {
        return SW_PARSE_READY;
    }
    if (connection->mgr == NULL) {
        return SW_PARSE_BAD_REQUEST;
    }

    config = &connection->mgr->config;
    data = sw_char_array_data(&connection->read_buffer);

    if (header_end == SIZE_MAX) {
        if (data_len > config->max_header_bytes) {
            return SW_PARSE_HEADERS_TOO_LARGE;
        }
        return SW_PARSE_PENDING;
    }
    if (header_end + 4 > config->max_header_bytes) {
        return SW_PARSE_HEADERS_TOO_LARGE;
    }
    if (!connection->headers_complete) {
        const f64 now_ms = sw_now_ms();
        connection->headers_complete = 1;
        connection->phase_started_at_ms = now_ms;
        sw_mgr_schedule_connection_timeout(connection->mgr, connection, now_ms);
    }

    first_line_end = sw_find_crlf_from(data, header_end, 0);
    if (first_line_end == SIZE_MAX || first_line_end > header_end) {
        return SW_PARSE_BAD_REQUEST;
    }

    memset(&parsed_request, 0, sizeof(parsed_request));
    s_array_init(&parsed_headers);

    {
        const c8* first_space = memchr(data, ' ', first_line_end);
        const c8* second_space = NULL;

        if (first_space != NULL) {
            second_space = memchr(first_space + 1, ' ', first_line_end - (sz)(first_space + 1 - data));
        }

        if (first_space == NULL || second_space == NULL) {
            goto bad_request;
        }

        parsed_request.method = sw_strdup_range(data, (sz)(first_space - data));
        parsed_request.uri = sw_strdup_range(first_space + 1, (sz)(second_space - first_space - 1));
        parsed_request.proto = sw_strdup_range(second_space + 1, (sz)(data + first_line_end - second_space - 1));
        if (parsed_request.method == NULL || parsed_request.uri == NULL || parsed_request.proto == NULL) {
            goto bad_request;
        }
    }

    cursor = first_line_end + 2;
    while (cursor < header_end) {
        const sz line_end = sw_find_crlf_from(data, header_end + 2, cursor);
        const c8* colon;
        const c8* name_end;
        const c8* value_begin;
        const c8* value_end;
        sw_http_header header;
        char* name;
        char* value;

        if (line_end == SIZE_MAX || line_end > header_end) {
            break;
        }
        if (cursor == line_end) {
            break;
        }

        if (s_array_get_size(&parsed_headers) + 1 > config->max_header_count) {
            goto too_many_headers;
        }

        colon = memchr(data + cursor, ':', line_end - cursor);
        if (colon == NULL) {
            goto bad_request;
        }

        name_end = colon;
        while (name_end > data + cursor && isspace((unsigned char)name_end[-1])) {
            --name_end;
        }
        if (name_end == data + cursor) {
            goto bad_request;
        }

        value_begin = colon + 1;
        while (value_begin < data + line_end && isspace((unsigned char)*value_begin)) {
            ++value_begin;
        }
        value_end = data + line_end;
        while (value_end > value_begin && isspace((unsigned char)value_end[-1])) {
            --value_end;
        }

        name = sw_strdup_range(data + cursor, (sz)(name_end - (data + cursor)));
        value = sw_strdup_range(value_begin, (sz)(value_end - value_begin));
        if (name == NULL || value == NULL) {
            free(name);
            free(value);
            goto bad_request;
        }
        header.name = name;
        header.value = value;
        s_array_add(&parsed_headers, header);

        if (sw_stricmp_ascii(name, "Content-Length") == 0) {
            sz content_length;
            if (!sw_parse_content_length_value(value, strlen(value), &content_length)) {
                goto bad_request;
            }
            if (saw_content_length && parsed_request.content_length != content_length) {
                goto bad_request;
            }
            parsed_request.content_length = content_length;
            saw_content_length = 1;
            if (parsed_request.content_length > config->max_body_bytes) {
                goto payload_too_large;
            }
        } else if (sw_stricmp_ascii(name, "Transfer-Encoding") == 0) {
            if (sw_http_header_value_has_token(value, "chunked")) {
                parsed_request.is_chunked = 1;
            }
        }

        cursor = line_end + 2;
    }

    if (parsed_request.is_chunked) {
        c8* chunked_body = NULL;
        sz chunked_body_len = 0;
        sz request_len = 0;
        sw_parse_result chunked_result;

        if (saw_content_length) {
            goto bad_request;
        }

        chunked_result = sw_decode_chunked_body(
            data,
            data_len,
            header_end + 4,
            config->max_body_bytes,
            config->max_header_bytes,
            &chunked_body,
            &chunked_body_len,
            &request_len);
        if (chunked_result != SW_PARSE_READY) {
            free(chunked_body);
            free((void*)parsed_request.method);
            free((void*)parsed_request.uri);
            free((void*)parsed_request.proto);
            sw_http_header_array_free_owned(&parsed_headers);
            return chunked_result;
        }

        parsed_request.body = chunked_body;
        parsed_request.body_len = chunked_body_len;
        parsed_request.content_length = chunked_body_len;
        parsed_request.headers = s_array_get_data(&parsed_headers);
        parsed_request.num_headers = s_array_get_size(&parsed_headers);

        connection->header_storage = parsed_headers;
        connection->request = parsed_request;
        connection->request_ready = 1;
        connection->parsed_request_bytes = request_len;
        return SW_PARSE_READY;
    }
    if (parsed_request.content_length > config->max_body_bytes) {
        goto payload_too_large;
    }
    if (parsed_request.content_length > SIZE_MAX - (header_end + 4)) {
        goto bad_request;
    }

    parsed_request.headers = s_array_get_data(&parsed_headers);
    parsed_request.num_headers = s_array_get_size(&parsed_headers);
    if (sw_request_body_should_stream(&parsed_request, config, header_end + 4)) {
        const sw_parse_result stream_result = sw_connection_begin_streamed_body(
            connection,
            &parsed_request,
            &parsed_headers,
            header_end + 4
        );
        if (stream_result != SW_PARSE_BAD_REQUEST) {
            return stream_result;
        }
        goto bad_request;
    }

    if (data_len < header_end + 4 + parsed_request.content_length) {
        goto pending;
    }

    parsed_request.body = sw_strdup_range(data + header_end + 4, parsed_request.content_length);
    if (parsed_request.content_length > 0 && parsed_request.body == NULL) {
        goto bad_request;
    }
    parsed_request.body_len = parsed_request.content_length;

    connection->header_storage = parsed_headers;
    connection->request = parsed_request;
    connection->request_ready = 1;
    connection->parsed_request_bytes = header_end + 4 + parsed_request.content_length;
    return SW_PARSE_READY;

pending:
    free((void*)parsed_request.method);
    free((void*)parsed_request.uri);
    free((void*)parsed_request.proto);
    sw_http_header_array_free_owned(&parsed_headers);
    return SW_PARSE_PENDING;

too_many_headers:
    free((void*)parsed_request.method);
    free((void*)parsed_request.uri);
    free((void*)parsed_request.proto);
    sw_http_header_array_free_owned(&parsed_headers);
    return SW_PARSE_TOO_MANY_HEADERS;

payload_too_large:
    free((void*)parsed_request.method);
    free((void*)parsed_request.uri);
    free((void*)parsed_request.proto);
    sw_http_header_array_free_owned(&parsed_headers);
    return SW_PARSE_PAYLOAD_TOO_LARGE;

bad_request:
    free((void*)parsed_request.method);
    free((void*)parsed_request.uri);
    free((void*)parsed_request.proto);
    sw_http_header_array_free_owned(&parsed_headers);
    return SW_PARSE_BAD_REQUEST;
}

static int sw_connection_fill_file_buffer(sw_connection* connection) {
    if (connection->file_stream == NULL || sw_char_array_size(&connection->write_buffer) > 0) {
        return 0;
    }

    while (connection->file_remaining > 0 && sw_char_array_size(&connection->write_buffer) == 0) {
        c8 chunk[16384];
        const sz to_read = (connection->file_remaining < sizeof(chunk)) ? connection->file_remaining : sizeof(chunk);
        const sz read_bytes = fread(chunk, 1, to_read, connection->file_stream);
        if (read_bytes == 0) {
            fclose(connection->file_stream);
            connection->file_stream = NULL;
            connection->file_remaining = 0;
            return 0;
        }
        connection->file_remaining -= read_bytes;
        if (!sw_connection_append_output(connection, chunk, read_bytes)) {
            fclose(connection->file_stream);
            connection->file_stream = NULL;
            connection->file_remaining = 0;
            return -1;
        }
    }

    if (connection->file_remaining == 0 && connection->file_stream != NULL) {
        fclose(connection->file_stream);
        connection->file_stream = NULL;
    }

    return 0;
}

int sw_mgr_accept_ready(sw_mgr* mgr, sw_listener* listener) {
    for (;;) {
        struct sockaddr_storage address;
        socklen_t address_len = (socklen_t)sizeof(address);
        sw_socket client_fd = accept(listener->fd, (struct sockaddr*)&address, &address_len);
        if (client_fd == SW_INVALID_SOCKET) {
            const int err = sw_socket_last_error();
            if (sw_socket_error_is_would_block(err)) {
                return 0;
            }
            return -1;
        }

        if (sw_socket_set_nonblocking(client_fd) != 0) {
            sw_socket_close(client_fd);
            continue;
        }

        {
            c8 host[64] = {0};
            c8 service[16] = {0};
            if (getnameinfo((struct sockaddr*)&address, address_len, host, sizeof(host), service, sizeof(service),
                NI_NUMERICHOST | NI_NUMERICSERV) != 0) {
                snprintf(host, sizeof(host), "unknown");
                snprintf(service, sizeof(service), "0");
            }

            if (!sw_mgr_try_reserve_connection(mgr)) {
                sw_socket_close(client_fd);
                continue;
            }

            if (sw_mgr_dispatch_connection(mgr, listener, client_fd, host, (u16)atoi(service), 1) != 0) {
                sw_mgr_release_connection(mgr);
                sw_socket_close(client_fd);
                return -1;
            }
        }
    }
}

static int sw_mgr_dispatch_connection(
    sw_mgr* mgr,
    sw_listener* listener,
    sw_socket fd,
    const c8* remote_ip,
    u16 remote_port,
    b8 counted
) {
    if (mgr == NULL || listener == NULL || fd == SW_INVALID_SOCKET) {
        return -1;
    }

    if (mgr->worker_count > 0 && mgr->workers != NULL) {
        sw_worker* worker = &mgr->workers[mgr->next_worker % mgr->worker_count];
        sw_pending_connection pending;

        memset(&pending, 0, sizeof(pending));
        pending.fd = fd;
        pending.listener = listener;
        pending.remote_port = remote_port;
        pending.counted = counted;
        if (remote_ip != NULL) {
            snprintf(pending.remote_ip, sizeof(pending.remote_ip), "%s", remote_ip);
        }

        mgr->next_worker = (mgr->next_worker + 1) % mgr->worker_count;
        s_mutex_lock(&worker->pending_lock);
        s_array_add(&worker->pending, pending);
        worker->accepted_count += 1;
        s_mutex_unlock(&worker->pending_lock);
        return 0;
    }

    if (sw_connection_create(mgr, listener, fd, remote_ip, remote_port, counted) == NULL) {
        return -1;
    }
    return 0;
}

static int sw_mgr_drain_pending_connections(sw_mgr* mgr) {
    sw_pending_connection_array pending;
    sz i;

    if (mgr == NULL || mgr->worker_owner == NULL) {
        return 0;
    }

    s_array_init(&pending);
    s_mutex_lock(&mgr->worker_owner->pending_lock);
    s_array_copy(&pending, &mgr->worker_owner->pending);
    s_array_clear(&mgr->worker_owner->pending);
    s_mutex_unlock(&mgr->worker_owner->pending_lock);

    for (i = 0; i < s_array_get_size(&pending); ++i) {
        sw_pending_connection* item = &s_array_get_data(&pending)[i];
        if (sw_connection_create(mgr, item->listener, item->fd, item->remote_ip, item->remote_port, item->counted) == NULL) {
            if (item->counted) {
                sw_mgr_release_connection(mgr);
            }
            sw_socket_close(item->fd);
        }
    }

    s_array_clear(&pending);
    return 0;
}

int sw_mgr_connection_readable(sw_mgr* mgr, sw_connection* connection) {
    c8 chunk[4096];
    b8 read_any = 0;

    if (connection->write_shutdown) {
        return sw_connection_discard_available_plain_input(mgr, connection);
    }

#if defined(SYPHAX_WEB_HAS_TLS)
    if (sw_connection_tls_handshake_pending(connection)) {
        const int tls_rc = sw_connection_tls_handshake(mgr, connection);
        if (tls_rc <= 0) {
            if (tls_rc < 0) {
                sw_mgr_close_connection(mgr, connection);
                return -1;
            }
            return 0;
        }
    }
    if (connection->tls != NULL && connection->tls_write_wants_read && sw_connection_has_pending_output(connection)) {
        return sw_mgr_connection_writable(mgr, connection);
    }
#endif

    for (;;) {
        int read_bytes = 0;
#if defined(SYPHAX_WEB_HAS_TLS)
        const int read_rc = sw_connection_transport_recv(connection, chunk, sizeof(chunk), &read_bytes);
        if (read_rc > 0) {
#else
        read_bytes = (int)recv(connection->fd, chunk, sizeof(chunk), 0);
        if (read_bytes > 0) {
#endif
            read_any = 1;
            if (!connection->request_started) {
                connection->request_started = 1;
                connection->phase_started_at_ms = sw_now_ms();
            }
            if (mgr->config.max_read_buffer_bytes > 0
                && ((sz)read_bytes > SIZE_MAX - sw_char_array_size(&connection->read_buffer)
                    || sw_char_array_size(&connection->read_buffer) + (sz)read_bytes > mgr->config.max_read_buffer_bytes)) {
                connection->must_close = 1;
                sw_http_reply_status_text(connection, 413, "Payload Too Large");
                return 0;
            }
            if (!sw_char_array_append_bytes(&connection->read_buffer, chunk, (sz)read_bytes)) {
                sw_mgr_close_connection(mgr, connection);
                return -1;
            }
            sw_connection_mark_activity(connection, sw_now_ms());
            if (sw_connection_process_input(mgr, connection) < 0) {
                return -1;
            }
            if (sw_connection_has_pending_output(connection) || connection->request_ready) {
                return 0;
            }
            continue;
        }
#if defined(SYPHAX_WEB_HAS_TLS)
        if (read_rc == -2) {
            sw_mgr_close_connection(mgr, connection);
            return -1;
        }
        if (read_rc == 0) {
            sw_mgr_sync_connection(mgr, connection);
            break;
        }
#else
        if (read_bytes == 0) {
            sw_mgr_close_connection(mgr, connection);
            return -1;
        }

        {
            const int err = sw_socket_last_error();
            if (sw_socket_error_is_would_block(err)) {
                break;
            }
        }
#endif

        sw_mgr_close_connection(mgr, connection);
        return -1;
    }

    if (!read_any && !connection->request_ready) {
        return 0;
    }

    return sw_connection_process_input(mgr, connection);
}

static int sw_connection_process_input(sw_mgr* mgr, sw_connection* connection) {
    if (mgr == NULL || connection == NULL || sw_connection_has_pending_output(connection)) {
        return 0;
    }
    if (connection->request_ready) {
        return sw_connection_dispatch_request(mgr, connection);
    }

    switch (sw_connection_try_parse_request(connection)) {
        case SW_PARSE_READY:
            return sw_connection_dispatch_request(mgr, connection);
        case SW_PARSE_PENDING:
            return 0;
        case SW_PARSE_HEADERS_TOO_LARGE:
        case SW_PARSE_TOO_MANY_HEADERS:
            connection->must_close = 1;
            sw_http_reply_status_text(connection, 431, "Request Header Fields Too Large");
            return 0;
        case SW_PARSE_PAYLOAD_TOO_LARGE:
            connection->must_close = 1;
            sw_http_reply_status_text(connection, 413, "Payload Too Large");
            return 0;
        case SW_PARSE_BAD_REQUEST:
        default:
            connection->must_close = 1;
            sw_http_replyf(connection, 400, "text/plain; charset=utf-8", "Bad Request");
            sw_mgr_sync_connection(mgr, connection);
            return 0;
    }
}

static int sw_connection_dispatch_request(sw_mgr* mgr, sw_connection* connection) {
    if (mgr == NULL || connection == NULL || !connection->request_ready || connection->request_dispatched) {
        return 0;
    }

    connection->request_dispatched = 1;
    if (connection->handler != NULL) {
        connection->handler(connection, &connection->request, connection->handler_user_data);
        return sw_mgr_sync_connection(mgr, connection);
    }
    return 0;
}

static int sw_connection_finish_response(sw_mgr* mgr, sw_connection* connection) {
    if (mgr == NULL || connection == NULL || sw_connection_has_pending_output(connection)) {
        return 0;
    }

    if (!connection->response_started) {
        if (connection->close_after_write) {
            return sw_connection_shutdown_send(mgr, connection);
        }
        return sw_mgr_sync_connection(mgr, connection);
    }

    connection->handled_requests += 1;
    if (connection->close_after_write) {
        return sw_connection_shutdown_send(mgr, connection);
    }

    if (connection->parsed_request_bytes > 0) {
        sw_char_array_consume_prefix(&connection->read_buffer, connection->parsed_request_bytes);
    }
    sw_connection_reset_request(connection);

    if (sw_char_array_size(&connection->read_buffer) > 0) {
        connection->request_started = 1;
        connection->phase_started_at_ms = sw_now_ms();
        return sw_connection_process_input(mgr, connection);
    }
    return sw_mgr_sync_connection(mgr, connection);
}

int sw_mgr_connection_writable(sw_mgr* mgr, sw_connection* connection) {
#if defined(SYPHAX_WEB_HAS_TLS)
    if (sw_connection_tls_handshake_pending(connection)) {
        const int tls_rc = sw_connection_tls_handshake(mgr, connection);
        if (tls_rc <= 0) {
            if (tls_rc < 0) {
                sw_mgr_close_connection(mgr, connection);
                return -1;
            }
            return 0;
        }
    }
    if (connection->tls != NULL && connection->tls_read_wants_write && !sw_connection_has_pending_output(connection)) {
        return sw_mgr_connection_readable(mgr, connection);
    }
#endif

    if (sw_connection_fill_file_buffer(connection) < 0) {
        sw_mgr_close_connection(mgr, connection);
        return -1;
    }

    while (sw_char_array_size(&connection->write_buffer) > 0) {
        const c8* data = sw_char_array_data(&connection->write_buffer);
        const sz data_len = sw_char_array_size(&connection->write_buffer);
        int sent_bytes = 0;
#if defined(SYPHAX_WEB_HAS_TLS)
        const int send_rc = sw_connection_transport_send(connection, data, data_len, &sent_bytes);
        if (send_rc > 0) {
#else
        sent_bytes = (int)send(connection->fd, data, (int)(data_len > (sz)INT_MAX ? (sz)INT_MAX : data_len), 0);
        if (sent_bytes > 0) {
#endif
            sw_connection_mark_activity(connection, sw_now_ms());
            sw_char_array_consume_prefix(&connection->write_buffer, (sz)sent_bytes);
            if (sw_char_array_size(&connection->write_buffer) == 0) {
                if (sw_connection_fill_file_buffer(connection) < 0) {
                    sw_mgr_close_connection(mgr, connection);
                    return -1;
                }
            }
            continue;
        }

#if defined(SYPHAX_WEB_HAS_TLS)
        if (send_rc == 0) {
            break;
        }
#else
        {
            const int err = sw_socket_last_error();
            if (sw_socket_error_is_would_block(err)) {
                break;
            }
        }
#endif

        sw_mgr_close_connection(mgr, connection);
        return -1;
    }

    if (sw_char_array_size(&connection->write_buffer) == 0) {
        if (sw_connection_fill_file_buffer(connection) < 0) {
            sw_mgr_close_connection(mgr, connection);
            return -1;
        }
    }

    if (sw_char_array_size(&connection->write_buffer) == 0 && connection->file_stream == NULL) {
        return sw_connection_finish_response(mgr, connection);
    }

    sw_mgr_sync_connection(mgr, connection);
    return 0;
}

sw_server_config sw_server_config_default(void) {
    const sw_server_config config = {
        .listen_backlog = 1024,
        .event_batch_size = 256,
        .worker_count = 1,
        .max_connections = 16384,
        .max_header_bytes = 16 * 1024,
        .max_body_bytes = 1024 * 1024,
        .max_header_count = 64,
        .max_read_buffer_bytes = 2 * 1024 * 1024,
        .max_write_buffer_bytes = 2 * 1024 * 1024,
        .initial_read_buffer_bytes = 4096,
        .initial_write_buffer_bytes = 4096,
        .keep_alive_max_requests = 1000,
        .idle_timeout_ms = 15 * 1000,
        .header_timeout_ms = 5 * 1000,
        .body_timeout_ms = 15 * 1000,
        .write_timeout_ms = 30 * 1000
    };
    return config;
}

sw_http_config sw_http_config_default(void) {
    return sw_server_config_default();
}

sw_tls_config sw_tls_config_default(void) {
    const sw_tls_config config = {
        .certificate_file = NULL,
        .private_key_file = NULL,
        .ca_file = NULL,
        .ca_path = NULL,
        .cipher_list = NULL,
        .ciphersuites = NULL,
        .verify_client = 0,
        .require_client_cert = 0,
        .handshake_timeout_ms = 5 * 1000
    };
    return config;
}

sw_mgr* sw_mgr_create(const sw_http_config* config) {
    return sw_mgr_create_internal(config, 0, NULL, NULL);
}

sw_server* sw_server_create(const sw_server_config* config) {
    return sw_mgr_create_internal(config, 1, NULL, NULL);
}

static sw_mgr* sw_mgr_create_internal(const sw_server_config* config, b8 server_owner, sw_mgr* root, sw_worker* worker_owner) {
    sw_mgr* mgr;
    const sw_server_config defaults = sw_server_config_default();

    if (sw_socket_runtime_acquire() != 0) {
        return NULL;
    }

    mgr = (sw_mgr*)calloc(1, sizeof(*mgr));
    if (mgr == NULL) {
        sw_socket_runtime_release();
        return NULL;
    }

    s_array_init(&mgr->listeners);
    s_array_init(&mgr->connections);
    s_array_init(&mgr->timers);
    mgr->config = defaults;
    mgr->root = root != NULL ? root : mgr;
    mgr->worker_owner = worker_owner;
    mgr->is_server_owner = server_owner;
    if (config != NULL) {
        if (config->listen_backlog > 0) {
            mgr->config.listen_backlog = config->listen_backlog;
        }
        if (config->event_batch_size > 0) {
            mgr->config.event_batch_size = config->event_batch_size;
        }
        if (config->worker_count > 0) {
            mgr->config.worker_count = config->worker_count;
        }
        if (config->max_connections > 0) {
            mgr->config.max_connections = config->max_connections;
        }
        if (config->max_header_bytes > 0) {
            mgr->config.max_header_bytes = config->max_header_bytes;
        }
        if (config->max_body_bytes > 0) {
            mgr->config.max_body_bytes = config->max_body_bytes;
        }
        if (config->max_header_count > 0) {
            mgr->config.max_header_count = config->max_header_count;
        }
        if (config->max_read_buffer_bytes > 0) {
            mgr->config.max_read_buffer_bytes = config->max_read_buffer_bytes;
        }
        if (config->max_write_buffer_bytes > 0) {
            mgr->config.max_write_buffer_bytes = config->max_write_buffer_bytes;
        }
        if (config->initial_read_buffer_bytes > 0) {
            mgr->config.initial_read_buffer_bytes = config->initial_read_buffer_bytes;
        }
        if (config->initial_write_buffer_bytes > 0) {
            mgr->config.initial_write_buffer_bytes = config->initial_write_buffer_bytes;
        }
        if (config->keep_alive_max_requests > 0) {
            mgr->config.keep_alive_max_requests = config->keep_alive_max_requests;
        }
        if (config->idle_timeout_ms > 0) {
            mgr->config.idle_timeout_ms = config->idle_timeout_ms;
        }
        if (config->header_timeout_ms > 0) {
            mgr->config.header_timeout_ms = config->header_timeout_ms;
        }
        if (config->body_timeout_ms > 0) {
            mgr->config.body_timeout_ms = config->body_timeout_ms;
        }
        if (config->write_timeout_ms > 0) {
            mgr->config.write_timeout_ms = config->write_timeout_ms;
        }
    }

    if (sw_backend_init(mgr) != 0) {
        s_array_clear(&mgr->listeners);
        s_array_clear(&mgr->connections);
        s_array_clear(&mgr->timers);
        free(mgr);
        sw_socket_runtime_release();
        return NULL;
    }

    return mgr;
}

void sw_mgr_destroy(sw_mgr* mgr) {
    sw_mgr_destroy_internal(mgr);
}

void sw_server_destroy(sw_server* server) {
    sw_mgr_destroy_internal(server);
}

static void sw_mgr_destroy_internal(sw_mgr* mgr) {
    sz worker_index;

    if (mgr == NULL) {
        return;
    }

    if (mgr->is_server_owner) {
        sw_server_stop(mgr);
        sw_server_wait(mgr);
    }

    while (s_array_get_size(&mgr->connections) > 0) {
        sw_connection* connection = s_array_get_data(&mgr->connections)[0];
        sw_mgr_close_connection(mgr, connection);
    }

    for (worker_index = 0; worker_index < mgr->worker_count; ++worker_index) {
        sw_worker* worker = &mgr->workers[worker_index];
        if (worker->mgr != NULL) {
            sw_mgr_destroy_internal(worker->mgr);
            worker->mgr = NULL;
        }
        s_array_clear(&worker->pending);
        s_mutex_destroy(&worker->pending_lock);
    }
    free(mgr->workers);
    mgr->workers = NULL;
    mgr->worker_count = 0;

    while (s_array_get_size(&mgr->listeners) > 0) {
        sw_listener* listener = s_array_get_data(&mgr->listeners)[0];
        sw_backend_unregister_listener(mgr, listener);
        if (listener->fd != SW_INVALID_SOCKET) {
            sw_socket_close(listener->fd);
        }
#if defined(SYPHAX_WEB_HAS_TLS)
        sw_tls_context_free(listener->tls_ctx);
#endif
        s_array_remove_ordered(&mgr->listeners, listener->array_handle);
        free(listener);
    }

    sw_backend_shutdown(mgr);
    s_array_clear(&mgr->listeners);
    s_array_clear(&mgr->connections);
    s_array_clear(&mgr->timers);
    free(mgr);
    sw_socket_runtime_release();
}

i32 sw_http_listen(sw_mgr* mgr, const c8* url, sw_http_handler handler, void* user_data) {
    c8 host[256];
    c8 port[32];
    u16 bound_port = 0;
    sw_socket listener_fd;
    sw_listener* listener;
    s_handle handle;

    if (mgr == NULL || url == NULL || handler == NULL) {
        return -1;
    }

    if (!sw_parse_listen_url(url, host, sizeof(host), port, sizeof(port))) {
        return -1;
    }

    listener_fd = sw_create_listener_socket(host, port, mgr->config.listen_backlog, &bound_port);
    if (listener_fd == SW_INVALID_SOCKET) {
        return -1;
    }

    listener = (sw_listener*)calloc(1, sizeof(*listener));
    if (listener == NULL) {
        sw_socket_close(listener_fd);
        return -1;
    }

    listener->source_kind = SW_SOURCE_LISTENER;
    listener->fd = listener_fd;
    listener->handler = handler;
    listener->handler_user_data = user_data;
    listener->mgr = mgr;
    listener->bound_port = bound_port;

    handle = s_array_add(&mgr->listeners, listener);
    listener->array_handle = handle;

    if (sw_backend_register_listener(mgr, listener) != 0) {
        s_array_remove_ordered(&mgr->listeners, handle);
        sw_socket_close(listener_fd);
        free(listener);
        return -1;
    }

    return 0;
}

i32 sw_server_add_http(sw_server* server, const c8* url, sw_http_handler handler, void* user_data) {
    return sw_http_listen(server, url, handler, user_data);
}

i32 sw_http_listen_tls(sw_mgr* mgr, const c8* url, const sw_tls_config* tls, sw_http_handler handler, void* user_data) {
#if defined(SYPHAX_WEB_HAS_TLS)
    c8 host[256];
    c8 port[32];
    u16 bound_port = 0;
    sw_socket listener_fd;
    sw_listener* listener;
    s_handle handle;
    sw_tls_config tls_config = sw_tls_config_default();
    SSL_CTX* tls_ctx;

    if (mgr == NULL || url == NULL || handler == NULL || tls == NULL) {
        return -1;
    }

    tls_config = *tls;
    if (tls_config.handshake_timeout_ms <= 0) {
        tls_config.handshake_timeout_ms = sw_tls_config_default().handshake_timeout_ms;
    }

    if (!sw_parse_listen_url_scheme(url, "https", 1, "443", host, sizeof(host), port, sizeof(port))) {
        return -1;
    }

    tls_ctx = sw_tls_context_create(&tls_config);
    if (tls_ctx == NULL) {
        return -1;
    }

    listener_fd = sw_create_listener_socket(host, port, mgr->config.listen_backlog, &bound_port);
    if (listener_fd == SW_INVALID_SOCKET) {
        sw_tls_context_free(tls_ctx);
        return -1;
    }

    listener = (sw_listener*)calloc(1, sizeof(*listener));
    if (listener == NULL) {
        sw_socket_close(listener_fd);
        sw_tls_context_free(tls_ctx);
        return -1;
    }

    listener->source_kind = SW_SOURCE_LISTENER;
    listener->fd = listener_fd;
    listener->handler = handler;
    listener->handler_user_data = user_data;
    listener->mgr = mgr;
    listener->bound_port = bound_port;
    listener->tls_enabled = 1;
    listener->tls_ctx = tls_ctx;
    listener->tls_handshake_timeout_ms = tls_config.handshake_timeout_ms;

    handle = s_array_add(&mgr->listeners, listener);
    listener->array_handle = handle;

    if (sw_backend_register_listener(mgr, listener) != 0) {
        s_array_remove_ordered(&mgr->listeners, handle);
        sw_socket_close(listener_fd);
        sw_tls_context_free(tls_ctx);
        free(listener);
        return -1;
    }

    return 0;
#else
    (void)mgr;
    (void)url;
    (void)tls;
    (void)handler;
    (void)user_data;
    return -1;
#endif
}

i32 sw_server_add_https(sw_server* server, const c8* url, const sw_tls_config* tls, sw_http_handler handler, void* user_data) {
    return sw_http_listen_tls(server, url, tls, handler, user_data);
}

static i32 sw_http_reply_status_text(sw_connection* connection, i32 status_code, const c8* body) {
    if (connection == NULL) {
        return -1;
    }
    if (sw_http_replyf(connection, status_code, "text/plain; charset=utf-8", "%s", (body != NULL) ? body : "") != 0) {
        if (connection->mgr != NULL) {
            sw_mgr_close_connection(connection->mgr, connection);
        }
        return -1;
    }
    return 0;
}

static i32 sw_mgr_effective_poll_timeout(sw_mgr* mgr, i32 timeout_ms) {
    i32 effective_timeout_ms = timeout_ms;
    const f64 now_ms = sw_now_ms();
    sw_timer_entry entry;

    if (mgr == NULL) {
        return timeout_ms;
    }
    if (sw_mgr_peek_timer(mgr, now_ms, &entry)) {
        const f64 remaining = entry.due_ms - now_ms;
        const i32 remaining_ms = remaining <= 0.0 ? 0 : (remaining > (f64)INT_MAX ? INT_MAX : (i32)remaining);
        if (effective_timeout_ms < 0 || remaining_ms < effective_timeout_ms) {
            effective_timeout_ms = remaining_ms;
        }
    }

    return effective_timeout_ms;
}

static void sw_mgr_expire_timeouts(sw_mgr* mgr) {
    const f64 now_ms = sw_now_ms();

    if (mgr == NULL) {
        return;
    }

    for (;;) {
        sw_timer_entry entry;
        sw_connection* connection;

        if (!sw_mgr_peek_timer(mgr, now_ms, &entry) || entry.due_ms > now_ms) {
            break;
        }
        sw_timer_heap_pop(&mgr->timers, NULL);
        {
            sw_connection** connection_slot = s_array_get(&mgr->connections, entry.connection_handle);
            connection = connection_slot != NULL ? *connection_slot : NULL;
        }
        if (connection == NULL || connection->timer_generation != entry.generation) {
            continue;
        }

        if (connection->write_shutdown || sw_connection_has_pending_output(connection)) {
            sw_mgr_close_connection(mgr, connection);
            continue;
        }
#if defined(SYPHAX_WEB_HAS_TLS)
        if (sw_connection_tls_handshake_pending(connection)) {
            sw_mgr_close_connection(mgr, connection);
            continue;
        }
#endif
        if (!connection->request_started) {
            sw_mgr_close_connection(mgr, connection);
            continue;
        }
        connection->must_close = 1;
        if (sw_connection_discard_available_plain_input(mgr, connection) < 0) {
            continue;
        }
        if (sw_http_reply_status_text(connection, 408, "Request Timeout") != 0) {
            continue;
        }
        if (sw_connection_has_pending_output(connection)
            && sw_mgr_connection_writable(mgr, connection) < 0) {
            continue;
        }
    }
}

i32 sw_mgr_poll(sw_mgr* mgr, i32 timeout_ms) {
    int rc;

    if (mgr == NULL || mgr->stop_requested) {
        return 0;
    }
    sw_mgr_drain_pending_connections(mgr);
    sw_mgr_expire_timeouts(mgr);
    timeout_ms = sw_mgr_effective_poll_timeout(mgr, timeout_ms);
    rc = sw_backend_poll(mgr, timeout_ms);
    if (rc < 0) {
        return -1;
    }
    sw_mgr_drain_pending_connections(mgr);
    sw_mgr_expire_timeouts(mgr);
    return rc;
}

void sw_mgr_request_stop(sw_mgr* mgr) {
    if (mgr != NULL) {
        mgr->stop_requested = 1;
    }
}

void sw_server_stop(sw_server* server) {
    sz worker_index;

    if (server == NULL) {
        return;
    }
    server->stop_requested = 1;
    for (worker_index = 0; worker_index < server->worker_count; ++worker_index) {
        if (server->workers[worker_index].mgr != NULL) {
            server->workers[worker_index].mgr->stop_requested = 1;
        }
    }
}

b8 sw_mgr_is_running(const sw_mgr* mgr) {
    return mgr != NULL && !mgr->stop_requested;
}

b8 sw_server_is_running(const sw_server* server) {
    return sw_mgr_is_running(server);
}

u16 sw_mgr_get_listener_port(const sw_mgr* mgr, sz listener_index) {
    sw_listener* const* listeners;
    if (mgr == NULL || listener_index >= s_array_get_size(&mgr->listeners)) {
        return 0;
    }
    listeners = (sw_listener* const*)s_array_get_data((sw_listener_array*)&mgr->listeners);
    return listeners[listener_index]->bound_port;
}

u16 sw_server_get_listener_port(const sw_server* server, sz listener_index) {
    return sw_mgr_get_listener_port(server, listener_index);
}

sz sw_server_open_connections(const sw_server* server) {
    return sw_mgr_active_connections(server);
}

sz sw_server_worker_count(const sw_server* server) {
    return server != NULL ? server->worker_count : 0;
}

sz sw_server_worker_open_connections(const sw_server* server, sz worker_index) {
    if (server == NULL || worker_index >= server->worker_count || server->workers[worker_index].mgr == NULL) {
        return 0;
    }
    return s_array_get_size(&server->workers[worker_index].mgr->connections);
}

sz sw_server_worker_accepted_connections(const sw_server* server, sz worker_index) {
    if (server == NULL || worker_index >= server->worker_count) {
        return 0;
    }
    return server->workers[worker_index].accepted_count;
}

static void* sw_worker_thread_main(void* arg) {
    sw_worker* worker = (sw_worker*)arg;

    while (worker != NULL && worker->owner != NULL && sw_mgr_is_running(worker->owner)) {
        if (sw_mgr_poll(worker->mgr, 50) < 0) {
            worker->owner->stop_requested = 1;
            break;
        }
    }

    return NULL;
}

static void* sw_accept_thread_main(void* arg) {
    sw_mgr* server = (sw_mgr*)arg;

    while (sw_mgr_is_running(server)) {
        if (sw_mgr_poll(server, 50) < 0) {
            server->stop_requested = 1;
            break;
        }
    }
    sw_server_stop(server);
    return NULL;
}

static i32 sw_server_start_workers(sw_mgr* server) {
    sz worker_index;
    const sz desired_workers = server != NULL ? server->config.worker_count : 0;

    if (server == NULL) {
        return -1;
    }
    if (server->worker_count > 0) {
        return 0;
    }
    if (desired_workers == 0) {
        return 0;
    }

    server->workers = (sw_worker*)calloc(desired_workers, sizeof(*server->workers));
    if (server->workers == NULL) {
        return -1;
    }
    for (worker_index = 0; worker_index < desired_workers; ++worker_index) {
        sw_worker* worker = &server->workers[worker_index];
        worker->owner = server;
        s_array_init(&worker->pending);
        if (!s_mutex_init(&worker->pending_lock)) {
            return -1;
        }
        server->worker_count += 1;
        worker->mgr = sw_mgr_create_internal(&server->config, 0, server, worker);
        if (worker->mgr == NULL) {
            return -1;
        }
        if (!s_thread_create(&worker->thread, sw_worker_thread_main, worker)) {
            return -1;
        }
        worker->thread_started = 1;
    }

    return 0;
}

static void sw_server_join_workers(sw_mgr* server) {
    sz worker_index;

    if (server == NULL) {
        return;
    }

    for (worker_index = 0; worker_index < server->worker_count; ++worker_index) {
        sw_worker* worker = &server->workers[worker_index];
        if (worker->thread_started) {
            (void)s_thread_join(&worker->thread, NULL);
            worker->thread_started = 0;
        }
    }
}

static void sw_mgr_close_all_connections(sw_mgr* mgr) {
    if (mgr == NULL) {
        return;
    }
    while (s_array_get_size(&mgr->connections) > 0) {
        sw_connection* connection = s_array_get_data(&mgr->connections)[0];
        sw_mgr_close_connection(mgr, connection);
    }
}

static void sw_server_close_worker_connections(sw_mgr* server) {
    sz worker_index;

    if (server == NULL) {
        return;
    }
    sw_mgr_close_all_connections(server);
    for (worker_index = 0; worker_index < server->worker_count; ++worker_index) {
        sw_mgr_close_all_connections(server->workers[worker_index].mgr);
    }
}

i32 sw_server_start(sw_server* server) {
    if (server == NULL || server->accept_thread_started || server->running_foreground) {
        return -1;
    }
    server->stop_requested = 0;
    if (sw_server_start_workers(server) != 0) {
        sw_server_stop(server);
        return -1;
    }
    if (!s_thread_create(&server->accept_thread, sw_accept_thread_main, server)) {
        sw_server_stop(server);
        sw_server_join_workers(server);
        return -1;
    }
    server->accept_thread_started = 1;
    server->server_started = 1;
    return 0;
}

i32 sw_server_wait(sw_server* server) {
    if (server == NULL) {
        return -1;
    }
    if (server->accept_thread_started) {
        (void)s_thread_join(&server->accept_thread, NULL);
        server->accept_thread_started = 0;
    }
    sw_server_stop(server);
    sw_server_join_workers(server);
    sw_server_close_worker_connections(server);
    server->server_started = 0;
    return 0;
}

i32 sw_server_poll(sw_server* server, i32 timeout_ms) {
    return sw_mgr_poll(server, timeout_ms);
}

i32 sw_server_run(sw_server* server) {
    i32 rc;

    if (server == NULL || server->accept_thread_started || server->running_foreground) {
        return -1;
    }
    server->stop_requested = 0;
    if (sw_server_start_workers(server) != 0) {
        sw_server_stop(server);
        return -1;
    }
    server->running_foreground = 1;
    rc = sw_server_run_loop(server);
    server->running_foreground = 0;
    sw_server_stop(server);
    sw_server_join_workers(server);
    sw_server_close_worker_connections(server);
    return rc;
}

i32 sw_server_listen(const c8* url, const sw_http_config* config, sw_http_handler handler, void* user_data) {
    sw_server* mgr = sw_server_create(config);
    int rc;

    if (mgr == NULL) {
        return -1;
    }

    rc = sw_server_add_http(mgr, url, handler, user_data);
    if (rc != 0) {
        sw_server_destroy(mgr);
        return rc;
    }

    rc = sw_server_run(mgr);
    sw_server_destroy(mgr);
    return rc;
}

static i32 sw_server_run_loop(sw_mgr* mgr) {
    i32 rc = 0;

    if (mgr == NULL) {
        return -1;
    }

    sw_signal_mgr = mgr;
#ifdef _WIN32
    SetConsoleCtrlHandler(sw_console_handler, TRUE);
#else
    signal(SIGINT, sw_signal_handler);
    signal(SIGTERM, sw_signal_handler);
#endif

    while (sw_mgr_is_running(mgr)) {
        if (sw_mgr_poll(mgr, 100) < 0) {
            rc = -1;
            break;
        }
    }

#ifdef _WIN32
    SetConsoleCtrlHandler(sw_console_handler, FALSE);
#else
    signal(SIGINT, SIG_DFL);
    signal(SIGTERM, SIG_DFL);
#endif
    sw_signal_mgr = NULL;

    return rc;
}

i32 sw_server_listen_tls(
    const c8* url,
    const sw_http_config* config,
    const sw_tls_config* tls,
    sw_http_handler handler,
    void* user_data
) {
    sw_server* mgr = sw_server_create(config);
    int rc;

    if (mgr == NULL) {
        return -1;
    }

    rc = sw_server_add_https(mgr, url, tls, handler, user_data);
    if (rc != 0) {
        sw_server_destroy(mgr);
        return rc;
    }

    rc = sw_server_run(mgr);
    sw_server_destroy(mgr);
    return rc;
}

static b8 sw_cookie_name_is_valid(const c8* name) {
    const c8* cursor = name;

    if (name == NULL || *name == '\0') {
        return 0;
    }
    while (*cursor != '\0') {
        const unsigned char ch = (unsigned char)*cursor;
        if (isalnum(ch) || ch == '!' || ch == '#' || ch == '$' || ch == '%' || ch == '&'
            || ch == '\'' || ch == '*' || ch == '+' || ch == '-' || ch == '.'
            || ch == '^' || ch == '_' || ch == '`' || ch == '|' || ch == '~') {
            ++cursor;
            continue;
        }
        return 0;
    }
    return 1;
}

static b8 sw_cookie_value_is_valid(const c8* value) {
    const c8* cursor = value;

    if (value == NULL) {
        return 0;
    }
    while (*cursor != '\0') {
        const unsigned char ch = (unsigned char)*cursor;
        if (ch < 0x21 || ch > 0x7e || ch == '"' || ch == ',' || ch == ';' || ch == '\\') {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

static b8 sw_cookie_attr_is_valid(const c8* value) {
    const c8* cursor = value;

    if (value == NULL) {
        return 1;
    }
    while (*cursor != '\0') {
        const unsigned char ch = (unsigned char)*cursor;
        if (ch < 0x20 || ch == 0x7f || ch == ';' || ch == '\r' || ch == '\n') {
            return 0;
        }
        ++cursor;
    }
    return 1;
}

static const c8* sw_cookie_same_site_name(sw_cookie_same_site same_site) {
    switch (same_site) {
        case SW_COOKIE_SAMESITE_LAX: return "Lax";
        case SW_COOKIE_SAMESITE_STRICT: return "Strict";
        case SW_COOKIE_SAMESITE_NONE: return "None";
        case SW_COOKIE_SAMESITE_UNSET:
        default: return NULL;
    }
}

static b8 sw_cookie_append_options(sw_char_array* out, sw_connection* connection, const sw_http_cookie* options) {
    char line[64];
    int written;
    const c8* same_site;
    const b8 secure = options->secure || (options->secure_auto && sw_connection_is_secure(connection));

    if (options->max_age >= 0) {
        written = snprintf(line, sizeof(line), "; Max-Age=%lld", (long long)options->max_age);
        if (written < 0 || (sz)written >= sizeof(line) || !sw_char_array_append_bytes(out, line, (sz)written)) {
            return 0;
        }
    }
    if (options->expires != NULL) {
        if (!sw_cookie_attr_is_valid(options->expires)
            || !sw_char_array_append_cstr(out, "; Expires=")
            || !sw_char_array_append_cstr(out, options->expires)) {
            return 0;
        }
    }
    if (options->domain != NULL) {
        if (!sw_cookie_attr_is_valid(options->domain)
            || !sw_char_array_append_cstr(out, "; Domain=")
            || !sw_char_array_append_cstr(out, options->domain)) {
            return 0;
        }
    }
    if (options->path != NULL) {
        if (!sw_cookie_attr_is_valid(options->path)
            || !sw_char_array_append_cstr(out, "; Path=")
            || !sw_char_array_append_cstr(out, options->path)) {
            return 0;
        }
    }
    if (secure && !sw_char_array_append_cstr(out, "; Secure")) {
        return 0;
    }
    if (options->http_only && !sw_char_array_append_cstr(out, "; HttpOnly")) {
        return 0;
    }
    same_site = sw_cookie_same_site_name(options->same_site);
    if (same_site != NULL
        && (!sw_char_array_append_cstr(out, "; SameSite=")
            || !sw_char_array_append_cstr(out, same_site))) {
        return 0;
    }
    return 1;
}

static b8 sw_request_allows_keep_alive(const sw_connection* connection) {
    const c8* connection_header;
    const sw_http_message* request;

    if (connection == NULL || connection->mgr == NULL) {
        return 0;
    }
    if (connection->must_close || connection->write_shutdown) {
        return 0;
    }
    if (connection->mgr->config.keep_alive_max_requests > 0
        && connection->handled_requests + 1 >= connection->mgr->config.keep_alive_max_requests) {
        return 0;
    }

    request = &connection->request;
    connection_header = sw_http_header_get(request, "Connection");
    if (request->proto != NULL && sw_stricmp_ascii(request->proto, "HTTP/1.0") == 0) {
        return connection_header != NULL && sw_strcasestr_ascii(connection_header, "keep-alive") != NULL;
    }
    if (connection_header != NULL && sw_strcasestr_ascii(connection_header, "close") != NULL) {
        return 0;
    }
    return 1;
}

static void sw_connection_prepare_response(sw_connection* connection) {
    if (connection == NULL) {
        return;
    }
    connection->response_started = 1;
    connection->close_after_write = !sw_request_allows_keep_alive(connection);
}

static i32 sw_http_queue_header(sw_connection* connection, const c8* name, const c8* value) {
    sw_http_header header;

    if (connection == NULL || name == NULL || value == NULL) {
        return -1;
    }
    if (sw_char_array_size(&connection->write_buffer) > 0 || connection->file_stream != NULL) {
        return -1;
    }

    header.name = sw_strdup_cstr(name);
    header.value = sw_strdup_cstr(value);
    if (header.name == NULL || header.value == NULL) {
        free((void*)header.name);
        free((void*)header.value);
        return -1;
    }
    s_array_add(&connection->response_headers, header);
    return 0;
}

static b8 sw_connection_can_append_output(const sw_connection* connection, sz add_len) {
    const sz current = connection != NULL ? sw_char_array_size(&connection->write_buffer) : 0;
    const sz max_size = (connection != NULL && connection->mgr != NULL) ? connection->mgr->config.max_write_buffer_bytes : 0;

    if (connection == NULL) {
        return 0;
    }
    if (max_size == 0) {
        return 1;
    }
    if (add_len > SIZE_MAX - current) {
        return 0;
    }
    return current + add_len <= max_size;
}

static b8 sw_connection_append_output(sw_connection* connection, const void* data, sz data_len) {
    if (data_len == 0) {
        return 1;
    }
    if (!sw_connection_can_append_output(connection, data_len)) {
        return 0;
    }
    return sw_char_array_append_bytes(&connection->write_buffer, data, data_len);
}

static b8 sw_connection_append_output_cstr(sw_connection* connection, const c8* text) {
    return text == NULL || sw_connection_append_output(connection, text, strlen(text));
}

sw_http_cookie sw_http_cookie_default(void) {
    const sw_http_cookie cookie = {
        .path = "/",
        .domain = NULL,
        .expires = NULL,
        .max_age = -1,
        .http_only = 1,
        .secure = 0,
        .secure_auto = 1,
        .same_site = SW_COOKIE_SAMESITE_LAX
    };
    return cookie;
}

static i32 sw_http_set_cookie_with_options(
    sw_connection* connection,
    const c8* name,
    const c8* value,
    const sw_http_cookie* options
) {
    sw_http_cookie effective;
    sw_char_array cookie;
    c8* cookie_value;
    i32 rc;

    if (connection == NULL || !sw_cookie_name_is_valid(name) || !sw_cookie_value_is_valid(value)) {
        return -1;
    }

    effective = (options != NULL) ? *options : sw_http_cookie_default();
    sw_char_array_init(&cookie);
    if (!sw_char_array_append_cstr(&cookie, name)
        || !sw_char_array_append_byte(&cookie, '=')
        || !sw_char_array_append_cstr(&cookie, value)
        || !sw_cookie_append_options(&cookie, connection, &effective)) {
        sw_char_array_free(&cookie);
        return -1;
    }

    cookie_value = sw_strdup_cstr(sw_char_array_data(&cookie));
    sw_char_array_free(&cookie);
    if (cookie_value == NULL) {
        return -1;
    }
    rc = sw_http_queue_header(connection, "Set-Cookie", cookie_value);
    free(cookie_value);
    return rc;
}

i32 sw_http_set_cookie(sw_connection* connection, const c8* name, const c8* value, const sw_http_cookie* options) {
    return sw_http_set_cookie_with_options(connection, name, value != NULL ? value : "", options);
}

i32 sw_http_clear_cookie(sw_connection* connection, const c8* name, const sw_http_cookie* options) {
    sw_http_cookie effective = (options != NULL) ? *options : sw_http_cookie_default();

    effective.max_age = 0;
    if (effective.expires == NULL) {
        effective.expires = "Thu, 01 Jan 1970 00:00:00 GMT";
    }
    return sw_http_set_cookie_with_options(connection, name, "", &effective);
}

static int sw_connection_begin_response(sw_connection* connection, i32 status_code, const c8* content_type, sz content_length) {
    char line[128];
    int written;
    sz i;

    if (connection == NULL) {
        return -1;
    }

    sw_char_array_reset(&connection->write_buffer);
    sw_connection_prepare_response(connection);

    written = snprintf(line, sizeof(line), "HTTP/1.1 %d %s\r\n", status_code, sw_http_status_text(status_code));
    if (written < 0 || (sz)written >= sizeof(line)
        || !sw_connection_append_output(connection, line, (sz)written)
        || !sw_connection_append_output_cstr(connection, "Content-Type: ")
        || !sw_connection_append_output_cstr(connection, (content_type != NULL) ? content_type : "text/plain; charset=utf-8")
        || !sw_connection_append_output_cstr(connection, "\r\n")) {
        sw_connection_clear_response_headers(connection);
        return -1;
    }

    written = snprintf(line, sizeof(line), "Content-Length: %zu\r\n", content_length);
    if (written < 0 || (sz)written >= sizeof(line)
        || !sw_connection_append_output(connection, line, (sz)written)) {
        sw_connection_clear_response_headers(connection);
        return -1;
    }

    for (i = 0; i < s_array_get_size(&connection->response_headers); ++i) {
        sw_http_header* header = &s_array_get_data(&connection->response_headers)[i];
        if (!sw_connection_append_output_cstr(connection, header->name)
            || !sw_connection_append_output_cstr(connection, ": ")
            || !sw_connection_append_output_cstr(connection, header->value)
            || !sw_connection_append_output_cstr(connection, "\r\n")) {
            sw_connection_clear_response_headers(connection);
            return -1;
        }
    }

    sw_connection_clear_response_headers(connection);
    if (connection->close_after_write) {
        return sw_connection_append_output_cstr(connection, "Connection: close\r\n\r\n") ? 0 : -1;
    }

    if (connection->mgr != NULL && connection->mgr->config.idle_timeout_ms > 0) {
        written = snprintf(line, sizeof(line), "Connection: keep-alive\r\nKeep-Alive: timeout=%d, max=%zu\r\n\r\n",
            connection->mgr->config.idle_timeout_ms / 1000,
            connection->mgr->config.keep_alive_max_requests);
        return written >= 0 && (sz)written < sizeof(line)
            && sw_connection_append_output(connection, line, (sz)written) ? 0 : -1;
    }

    return sw_connection_append_output_cstr(connection, "Connection: keep-alive\r\n\r\n") ? 0 : -1;
}

i32 sw_http_reply(sw_connection* connection, i32 status_code, const c8* content_type, const void* body, sz body_len) {
    if (connection == NULL) {
        return -1;
    }
    if (sw_connection_begin_response(connection, status_code, content_type, body_len) != 0) {
        return -1;
    }
    if (body_len > 0 && !sw_connection_append_output(connection, body, body_len)) {
        return -1;
    }
    sw_connection_mark_activity(connection, sw_now_ms());
    return sw_mgr_sync_connection(connection->mgr, connection);
}

i32 sw_http_replyf(sw_connection* connection, i32 status_code, const c8* content_type, const c8* fmt, ...) {
    sw_char_array body;
    va_list ap;
    int rc;

    if (connection == NULL || fmt == NULL) {
        return -1;
    }

    sw_char_array_init(&body);
    va_start(ap, fmt);
    if (!sw_char_array_append_vformat(&body, fmt, ap)) {
        va_end(ap);
        sw_char_array_free(&body);
        return -1;
    }
    va_end(ap);

    rc = sw_http_reply(connection, status_code, content_type, sw_char_array_data(&body), sw_char_array_size(&body));
    sw_char_array_free(&body);
    return rc;
}

i32 sw_http_write(sw_connection* connection, const void* data, sz data_len) {
    if (connection == NULL) {
        return -1;
    }
    connection->must_close = 1;
    connection->close_after_write = 1;
    if (!sw_connection_append_output(connection, data, data_len)) {
        return -1;
    }
    sw_connection_mark_activity(connection, sw_now_ms());
    return sw_mgr_sync_connection(connection->mgr, connection);
}

i32 sw_http_printf(sw_connection* connection, const c8* fmt, ...) {
    va_list ap;
    b8 ok;

    if (connection == NULL || fmt == NULL) {
        return -1;
    }

    va_start(ap, fmt);
    ok = sw_char_array_append_vformat(&connection->write_buffer, fmt, ap);
    va_end(ap);

    connection->must_close = 1;
    connection->close_after_write = 1;
    if (!ok || !sw_connection_can_append_output(connection, 0)) {
        return -1;
    }
    sw_connection_mark_activity(connection, sw_now_ms());
    return sw_mgr_sync_connection(connection->mgr, connection);
}

static b8 sw_path_is_separator(c8 ch) {
    return ch == '/' || ch == '\\';
}

static b8 sw_path_join(char* out, sz out_cap, const c8* lhs, const c8* rhs) {
    const sz lhs_len = lhs != NULL ? strlen(lhs) : 0;
    const b8 need_sep = lhs_len > 0 && !sw_path_is_separator(lhs[lhs_len - 1]);
    const int written = snprintf(out, out_cap, "%s%s%s", lhs != NULL ? lhs : "", need_sep ? "/" : "", rhs != NULL ? rhs : "");
    return written >= 0 && (sz)written < out_cap;
}

static b8 sw_path_real(const c8* path, char* out, sz out_cap) {
#ifdef _WIN32
    DWORD written;

    if (path == NULL || out == NULL || out_cap == 0) {
        return 0;
    }
    written = GetFullPathNameA(path, (DWORD)out_cap, out, NULL);
    return written > 0 && (sz)written < out_cap;
#else
    char* resolved;
    sz resolved_len;

    if (path == NULL || out == NULL || out_cap == 0) {
        return 0;
    }
    resolved = realpath(path, NULL);
    if (resolved == NULL) {
        return 0;
    }
    resolved_len = strlen(resolved);
    if (resolved_len >= out_cap) {
        free(resolved);
        return 0;
    }
    memcpy(out, resolved, resolved_len + 1);
    free(resolved);
    return 1;
#endif
}

static b8 sw_path_has_prefix(const c8* path, const c8* prefix) {
    sz i;
    const sz prefix_len = prefix != NULL ? strlen(prefix) : 0;

    if (path == NULL || prefix == NULL) {
        return 0;
    }

    for (i = 0; i < prefix_len; ++i) {
#ifdef _WIN32
        if (tolower((unsigned char)path[i]) != tolower((unsigned char)prefix[i])) {
#else
        if (path[i] != prefix[i]) {
#endif
            return 0;
        }
    }

    if (path[prefix_len] == '\0') {
        return 1;
    }
    if (prefix_len > 0 && sw_path_is_separator(prefix[prefix_len - 1])) {
        return 1;
    }
    return sw_path_is_separator(path[prefix_len]);
}

static b8 sw_decode_request_path(const c8* request_path, char* out, sz out_cap) {
    sw_char_array decoded;
    const c8* data;
    sz path_len;
    sz i = 0;
    sz out_len = 0;

    if (request_path == NULL || out == NULL || out_cap == 0) {
        return 0;
    }

    path_len = sw_http_path_length(request_path);
    s_array_init(&decoded);
    while (i < path_len) {
        c8 ch = request_path[i];

        if (ch == '%') {
            const int hi = (i + 1 < path_len) ? sw_hex_value(request_path[i + 1]) : -1;
            const int lo = (i + 2 < path_len) ? sw_hex_value(request_path[i + 2]) : -1;
            if (hi < 0 || lo < 0) {
                sw_char_array_free(&decoded);
                return 0;
            }
            ch = (c8)((hi << 4) | lo);
            i += 3;
        } else {
            i += 1;
        }

        if (ch == '\0') {
            sw_char_array_free(&decoded);
            return 0;
        }
        sw_char_array_append_byte(&decoded, ch);
    }

    data = sw_char_array_data(&decoded);
    i = 0;
    while (i < sw_char_array_size(&decoded)) {
        sz segment_begin;
        sz segment_end;
        sz segment_len;

        while (i < sw_char_array_size(&decoded) && sw_path_is_separator(data[i])) {
            ++i;
        }
        segment_begin = i;
        while (i < sw_char_array_size(&decoded) && !sw_path_is_separator(data[i])) {
            ++i;
        }
        segment_end = i;
        segment_len = segment_end - segment_begin;
        if (segment_len == 0) {
            continue;
        }
        if (segment_len == 1 && data[segment_begin] == '.') {
            continue;
        }
        if (segment_len == 2 && data[segment_begin] == '.' && data[segment_begin + 1] == '.') {
            sw_char_array_free(&decoded);
            return 0;
        }
        if (out_len != 0) {
            if (out_len + 1 >= out_cap) {
                sw_char_array_free(&decoded);
                return 0;
            }
            out[out_len++] = '/';
        }
        if (out_len + segment_len >= out_cap) {
            sw_char_array_free(&decoded);
            return 0;
        }
        memcpy(out + out_len, data + segment_begin, segment_len);
        out_len += segment_len;
    }

    out[out_len] = '\0';
    sw_char_array_free(&decoded);
    return out_len > 0;
}

static const c8* sw_guess_content_type(const c8* path) {
    const c8* ext = strrchr(path, '.');
    if (ext == NULL) return "application/octet-stream";
    if (sw_stricmp_ascii(ext, ".html") == 0 || sw_stricmp_ascii(ext, ".htm") == 0) return "text/html; charset=utf-8";
    if (sw_stricmp_ascii(ext, ".css") == 0) return "text/css; charset=utf-8";
    if (sw_stricmp_ascii(ext, ".js") == 0) return "application/javascript; charset=utf-8";
    if (sw_stricmp_ascii(ext, ".json") == 0) return "application/json; charset=utf-8";
    if (sw_stricmp_ascii(ext, ".png") == 0) return "image/png";
    if (sw_stricmp_ascii(ext, ".jpg") == 0 || sw_stricmp_ascii(ext, ".jpeg") == 0) return "image/jpeg";
    if (sw_stricmp_ascii(ext, ".gif") == 0) return "image/gif";
    if (sw_stricmp_ascii(ext, ".svg") == 0) return "image/svg+xml";
    if (sw_stricmp_ascii(ext, ".txt") == 0) return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

static i32 sw_http_send_file(sw_connection* connection, const c8* path) {
    struct stat st;
    FILE* file;

    if (connection == NULL || path == NULL) {
        return -1;
    }

    if (stat(path, &st) != 0) {
        return sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "File not found");
    }

    if (S_ISDIR(st.st_mode)) {
        return sw_http_replyf(connection, 403, "text/plain; charset=utf-8", "Directory listing not allowed");
    }
    if (st.st_size < 0 || (uintmax_t)st.st_size > (uintmax_t)SIZE_MAX) {
        return sw_http_reply_status_text(connection, 413, "Payload Too Large");
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return sw_http_replyf(connection, 403, "text/plain; charset=utf-8", "Cannot open file");
    }

    if (sw_connection_begin_response(connection, 200, sw_guess_content_type(path), (sz)st.st_size) != 0) {
        fclose(file);
        return -1;
    }

    if (connection->file_stream != NULL) {
        fclose(connection->file_stream);
    }
    connection->file_stream = file;
    connection->file_remaining = (sz)st.st_size;
    if (sw_connection_fill_file_buffer(connection) < 0) {
        return -1;
    }
    sw_connection_mark_activity(connection, sw_now_ms());
    return sw_mgr_sync_connection(connection->mgr, connection);
}

i32 sw_http_serve_path(sw_connection* connection, const c8* docroot, const c8* request_path) {
    char docroot_real[4096];
    char relative_path[4096];
    char joined_path[4096];
    char target_real[4096];
    struct stat st;

    if (connection == NULL || docroot == NULL || request_path == NULL) {
        return -1;
    }
    if (!sw_path_real(docroot, docroot_real, sizeof(docroot_real))) {
        return sw_http_reply_status_text(connection, 500, "Static asset root is not available");
    }
    if (!sw_decode_request_path(request_path, relative_path, sizeof(relative_path))) {
        return sw_http_reply_status_text(connection, 403, "Forbidden");
    }
    if (!sw_path_join(joined_path, sizeof(joined_path), docroot_real, relative_path)) {
        return sw_http_reply_status_text(connection, 404, "File not found");
    }
    if (stat(joined_path, &st) != 0) {
        return sw_http_reply_status_text(connection, 404, "File not found");
    }
    if (!sw_path_real(joined_path, target_real, sizeof(target_real))) {
        return sw_http_reply_status_text(connection, 404, "File not found");
    }
    if (!sw_path_has_prefix(target_real, docroot_real)) {
        return sw_http_reply_status_text(connection, 403, "Forbidden");
    }
    return sw_http_send_file(connection, target_real);
}

const c8* sw_http_header_get(const sw_http_message* hm, const c8* name) {
    sz i;
    if (hm == NULL || name == NULL) {
        return NULL;
    }
    for (i = 0; i < hm->num_headers; ++i) {
        if (sw_stricmp_ascii(hm->headers[i].name, name) == 0) {
            return hm->headers[i].value;
        }
    }
    return NULL;
}

static i32 sw_http_copy_cookie_value(const c8* value, sz value_len, c8* buf, sz buf_len) {
    sz cursor = 0;
    sz written = 0;
    const b8 quoted = value_len >= 2 && value[0] == '"' && value[value_len - 1] == '"';

    if (buf == NULL || buf_len == 0) {
        return -1;
    }

    buf[0] = '\0';
    if (quoted) {
        cursor = 1;
        value_len -= 1;
    }

    while (cursor < value_len) {
        c8 ch = value[cursor++];
        if (quoted && ch == '\\' && cursor < value_len) {
            ch = value[cursor++];
        }
        if (written + 1 < buf_len) {
            buf[written++] = ch;
        }
    }
    buf[written] = '\0';
    return (i32)written;
}

i32 sw_http_get_cookie(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len) {
    const sz name_len = (name != NULL) ? strlen(name) : 0;
    sz i;

    if (buf != NULL && buf_len > 0) {
        buf[0] = '\0';
    }
    if (hm == NULL || name == NULL || name_len == 0 || buf == NULL || buf_len == 0) {
        return -1;
    }

    for (i = 0; i < hm->num_headers; ++i) {
        const c8* cursor;
        const c8* end;

        if (hm->headers[i].name == NULL || hm->headers[i].value == NULL
            || sw_stricmp_ascii(hm->headers[i].name, "Cookie") != 0) {
            continue;
        }

        cursor = hm->headers[i].value;
        end = cursor + strlen(cursor);
        while (cursor < end) {
            const c8* name_begin;
            const c8* name_end;
            const c8* value_begin;
            const c8* value_end;

            while (cursor < end && (*cursor == ';' || isspace((unsigned char)*cursor))) {
                ++cursor;
            }
            name_begin = cursor;
            while (cursor < end && *cursor != '=' && *cursor != ';') {
                ++cursor;
            }
            name_end = cursor;
            while (name_end > name_begin && isspace((unsigned char)name_end[-1])) {
                --name_end;
            }
            if (cursor >= end || *cursor != '=') {
                while (cursor < end && *cursor != ';') {
                    ++cursor;
                }
                continue;
            }

            ++cursor;
            while (cursor < end && isspace((unsigned char)*cursor)) {
                ++cursor;
            }
            value_begin = cursor;
            if (cursor < end && *cursor == '"') {
                ++cursor;
                while (cursor < end) {
                    if (*cursor == '\\' && cursor + 1 < end) {
                        cursor += 2;
                        continue;
                    }
                    if (*cursor == '"') {
                        ++cursor;
                        break;
                    }
                    ++cursor;
                }
                value_end = cursor;
            } else {
                while (cursor < end && *cursor != ';') {
                    ++cursor;
                }
                value_end = cursor;
                while (value_end > value_begin && isspace((unsigned char)value_end[-1])) {
                    --value_end;
                }
            }

            if ((sz)(name_end - name_begin) == name_len && strncmp(name_begin, name, name_len) == 0) {
                return sw_http_copy_cookie_value(value_begin, (sz)(value_end - value_begin), buf, buf_len);
            }

            while (cursor < end && *cursor != ';') {
                ++cursor;
            }
        }
    }

    return 0;
}

static sz sw_http_path_length(const c8* uri) {
    const c8* cursor = uri;

    if (cursor == NULL) {
        return 0;
    }

    while (*cursor != '\0' && *cursor != '?') {
        ++cursor;
    }

    return (sz)(cursor - uri);
}

static const c8* sw_http_query_string(const sw_http_message* hm, sz* query_len) {
    const c8* query_begin;
    const c8* query_end;

    if (query_len != NULL) {
        *query_len = 0;
    }
    if (hm == NULL || hm->uri == NULL) {
        return NULL;
    }

    query_begin = strchr(hm->uri, '?');
    if (query_begin == NULL) {
        return NULL;
    }
    query_begin += 1;

    query_end = strchr(query_begin, '#');
    if (query_end == NULL) {
        query_end = query_begin + strlen(query_begin);
    }

    if (query_len != NULL) {
        *query_len = (sz)(query_end - query_begin);
    }
    return query_begin;
}

static int sw_hex_value(c8 ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

static i32 sw_http_decode_var(const c8* source, sz source_len, const c8* name, c8* buf, sz buf_len, i32 missing_value) {
    const sz name_len = (name != NULL) ? strlen(name) : 0;
    const c8* cursor;
    const c8* source_end;

    if (source == NULL || name == NULL || buf == NULL || buf_len == 0) {
        return -1;
    }

    buf[0] = '\0';
    cursor = source;
    source_end = source + source_len;
    while (cursor < source_end) {
        const c8* pair_end = memchr(cursor, '&', (sz)(source_end - cursor));
        const c8* equals = NULL;
        const c8* value_begin;
        sz written = 0;

        if (pair_end == NULL) {
            pair_end = source_end;
        }

        equals = memchr(cursor, '=', (sz)(pair_end - cursor));
        if (equals != NULL && (sz)(equals - cursor) == name_len && strncmp(cursor, name, name_len) == 0) {
            value_begin = equals + 1;
            while (value_begin < pair_end && written + 1 < buf_len) {
                if (*value_begin == '+' ) {
                    buf[written++] = ' ';
                    ++value_begin;
                } else if (*value_begin == '%' && value_begin + 2 < pair_end) {
                    const int hi = sw_hex_value(value_begin[1]);
                    const int lo = sw_hex_value(value_begin[2]);
                    if (hi >= 0 && lo >= 0) {
                        buf[written++] = (c8)((hi << 4) | lo);
                        value_begin += 3;
                    } else {
                        buf[written++] = *value_begin++;
                    }
                } else {
                    buf[written++] = *value_begin++;
                }
            }

            buf[written] = '\0';
            return (i32)written;
        }

        if (pair_end == source_end) {
            break;
        }
        cursor = pair_end + 1;
    }

    return missing_value;
}

static c8* sw_strdup_trimmed_range(const c8* text, sz text_len) {
    sz begin = 0;
    sz end = text_len;

    if (text == NULL) {
        return NULL;
    }
    while (begin < text_len && isspace((unsigned char)text[begin])) {
        ++begin;
    }
    while (end > begin && isspace((unsigned char)text[end - 1])) {
        --end;
    }
    return sw_strdup_range(text + begin, end - begin);
}

static b8 sw_header_value_matches_name(const c8* line, sz line_len, const c8* name) {
    sz i;
    const sz name_len = name != NULL ? strlen(name) : 0;

    if (line == NULL || name == NULL || line_len <= name_len) {
        return 0;
    }
    for (i = 0; i < name_len; ++i) {
        if (tolower((unsigned char)line[i]) != tolower((unsigned char)name[i])) {
            return 0;
        }
    }
    return line[name_len] == ':';
}

static c8* sw_parse_disposition_param(const c8* line, sz line_len, const c8* key) {
    const c8* cursor;
    const c8* end;
    const sz key_len = key != NULL ? strlen(key) : 0;

    if (line == NULL || key == NULL || key_len == 0) {
        return NULL;
    }

    cursor = memchr(line, ':', line_len);
    if (cursor == NULL) {
        return NULL;
    }
    cursor += 1;
    end = line + line_len;

    while (cursor < end) {
        const c8* key_begin;
        const c8* key_end;
        const c8* value_begin;
        const c8* value_end;
        b8 key_matches;

        while (cursor < end && (isspace((unsigned char)*cursor) || *cursor == ';')) {
            ++cursor;
        }
        key_begin = cursor;
        while (cursor < end && *cursor != '=' && *cursor != ';') {
            ++cursor;
        }
        key_end = cursor;
        while (key_end > key_begin && isspace((unsigned char)key_end[-1])) {
            --key_end;
        }
        if (cursor >= end || *cursor != '=') {
            while (cursor < end && *cursor != ';') {
                ++cursor;
            }
            continue;
        }
        cursor += 1;
        key_matches = ((sz)(key_end - key_begin) == key_len) && sw_ascii_case_equal_range(key_begin, key, key_len);

        while (cursor < end && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if (cursor < end && *cursor == '"') {
            value_begin = ++cursor;
            while (cursor < end && *cursor != '"') {
                if (*cursor == '\\' && cursor + 1 < end) {
                    cursor += 2;
                    continue;
                }
                ++cursor;
            }
            value_end = cursor;
        } else {
            value_begin = cursor;
            while (cursor < end && *cursor != ';') {
                ++cursor;
            }
            value_end = cursor;
        }
        if (key_matches) {
            return sw_strdup_trimmed_range(value_begin, (sz)(value_end - value_begin));
        }
        while (cursor < end && *cursor != ';') {
            ++cursor;
        }
    }

    return NULL;
}

static b8 sw_http_content_type_boundary(const c8* content_type, char* boundary, sz boundary_cap) {
    const c8* cursor;
    const c8* end;

    if (content_type == NULL || boundary == NULL || boundary_cap == 0) {
        return 0;
    }

    cursor = content_type;
    end = content_type + strlen(content_type);
    while (cursor < end) {
        const c8* key_begin;
        const c8* key_end;
        const c8* value_begin;
        const c8* value_end;

        while (cursor < end && (isspace((unsigned char)*cursor) || *cursor == ';')) {
            ++cursor;
        }
        key_begin = cursor;
        while (cursor < end && *cursor != '=' && *cursor != ';') {
            ++cursor;
        }
        key_end = cursor;
        while (key_end > key_begin && isspace((unsigned char)key_end[-1])) {
            --key_end;
        }
        if (cursor >= end || *cursor != '=') {
            while (cursor < end && *cursor != ';') {
                ++cursor;
            }
            continue;
        }
        cursor += 1;
        while (cursor < end && isspace((unsigned char)*cursor)) {
            ++cursor;
        }
        if ((sz)(key_end - key_begin) != 8 || !sw_ascii_case_equal_range(key_begin, "boundary", 8)) {
            while (cursor < end && *cursor != ';') {
                if (*cursor == '"' && cursor + 1 < end) {
                    ++cursor;
                }
                ++cursor;
            }
            continue;
        }

        if (cursor < end && *cursor == '"') {
            value_begin = ++cursor;
            while (cursor < end && *cursor != '"') {
                ++cursor;
            }
            value_end = cursor;
        } else {
            value_begin = cursor;
            while (cursor < end && *cursor != ';') {
                ++cursor;
            }
            value_end = cursor;
        }

        if (value_begin == value_end || (sz)(value_end - value_begin) >= boundary_cap) {
            return 0;
        }
        memcpy(boundary, value_begin, (sz)(value_end - value_begin));
        boundary[value_end - value_begin] = '\0';
        return 1;
    }

    return 0;
}

static b8 sw_http_content_type_matches(const c8* content_type, const c8* expected) {
    const c8* begin = content_type;
    const c8* end;
    const sz expected_len = expected != NULL ? strlen(expected) : 0;

    if (content_type == NULL || expected == NULL || expected_len == 0) {
        return 0;
    }

    while (*begin != '\0' && isspace((unsigned char)*begin)) {
        ++begin;
    }
    end = begin;
    while (*end != '\0' && *end != ';') {
        ++end;
    }
    while (end > begin && isspace((unsigned char)end[-1])) {
        --end;
    }
    return (sz)(end - begin) == expected_len && sw_ascii_case_equal_range(begin, expected, expected_len);
}

static const c8* sw_find_multipart_boundary(
    const c8* data,
    sz data_len,
    sz offset,
    const c8* boundary_marker,
    sz boundary_marker_len,
    b8 require_crlf_prefix
) {
    sz pos = offset;

    while (pos < data_len) {
        pos = sw_find_bytes_from(data, data_len, pos, boundary_marker, boundary_marker_len);
        if (pos == SIZE_MAX) {
            return NULL;
        }
        if (!require_crlf_prefix && pos == offset) {
            return data + pos;
        }
        if (require_crlf_prefix && pos >= 2 && data[pos - 2] == '\r' && data[pos - 1] == '\n') {
            return data + pos;
        }
        pos += 1;
    }

    return NULL;
}

b8 sw_http_is(const sw_http_message* hm, const c8* method, const c8* path) {
    const sz request_path_len = sw_http_path_length((hm != NULL) ? hm->uri : NULL);
    const sz path_len = (path != NULL) ? strlen(path) : 0;

    if (hm == NULL || hm->method == NULL || hm->uri == NULL || method == NULL || path == NULL) {
        return 0;
    }

    return strcmp(hm->method, method) == 0
        && request_path_len == path_len
        && strncmp(hm->uri, path, path_len) == 0;
}

i32 sw_http_get_query(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len) {
    const c8* query;
    sz query_len;

    if (buf != NULL && buf_len > 0) {
        buf[0] = '\0';
    }
    if (hm == NULL || name == NULL || buf == NULL || buf_len == 0) {
        return -1;
    }

    query = sw_http_query_string(hm, &query_len);
    if (query == NULL || query_len == 0) {
        return 0;
    }

    return sw_http_decode_var(query, query_len, name, buf, buf_len, 0);
}

static i32 sw_http_copy_multipart_data(const sw_http_multipart* mp, c8* buf, sz buf_len) {
    sz copy_len;

    if (mp == NULL || buf == NULL || buf_len == 0) {
        return -1;
    }

    copy_len = mp->data_len < buf_len - 1 ? mp->data_len : buf_len - 1;
    if (mp->data != NULL) {
        memcpy(buf, mp->data, copy_len);
    } else if (copy_len > 0) {
        return -1;
    }
    buf[copy_len] = '\0';
    return (i32)copy_len;
}

static i32 sw_http_get_multipart_form(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len) {
    sw_http_multipart part;
    sz offset = 0;

    memset(&part, 0, sizeof(part));
    while (sw_http_next_multipart(hm, &part, &offset)) {
        if (part.filename == NULL && part.name != NULL && strcmp(part.name, name) == 0) {
            const i32 copied = sw_http_copy_multipart_data(&part, buf, buf_len);
            sw_http_multipart_clear(&part);
            return copied;
        }
        sw_http_multipart_clear(&part);
        memset(&part, 0, sizeof(part));
    }

    return 0;
}

i32 sw_http_get_form(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len) {
    char boundary[256];
    const c8* content_type;

    if (buf != NULL && buf_len > 0) {
        buf[0] = '\0';
    }
    if (hm == NULL || name == NULL || buf == NULL || buf_len == 0) {
        return -1;
    }
    content_type = sw_http_header_get(hm, "Content-Type");
    if (sw_http_content_type_matches(content_type, "multipart/form-data")
        && sw_http_content_type_boundary(content_type, boundary, sizeof(boundary))) {
        return sw_http_get_multipart_form(hm, name, buf, buf_len);
    }
    if (hm->body == NULL || hm->body_len == 0) {
        return 0;
    }

    return sw_http_decode_var(hm->body, hm->body_len, name, buf, buf_len, 0);
}

i32 sw_http_next_multipart(const sw_http_message* hm, sw_http_multipart* mp, sz* offset) {
    const c8* content_type;
    char boundary[256];
    char boundary_marker[260];
    char next_boundary_marker[262];
    const c8* part_boundary;
    const c8* headers_end;
    const c8* body_start;
    const c8* next_boundary;
    sz line_offset;
    sz part_offset;
    sz headers_end_offset;
    const sz body_len = hm != NULL ? hm->body_len : 0;

    if (hm == NULL || mp == NULL || offset == NULL || *offset > body_len) {
        return 0;
    }

    content_type = sw_http_header_get(hm, "Content-Type");
    if (content_type == NULL
        || !sw_http_content_type_matches(content_type, "multipart/form-data")
        || !sw_http_content_type_boundary(content_type, boundary, sizeof(boundary))) {
        return 0;
    }
    if (hm->body == NULL) {
        return 0;
    }
    {
        const int written = snprintf(boundary_marker, sizeof(boundary_marker), "--%s", boundary);
        if (written < 0 || (sz)written >= sizeof(boundary_marker)) {
            return 0;
        }
    }
    {
        const int written = snprintf(next_boundary_marker, sizeof(next_boundary_marker), "\r\n%s", boundary_marker);
        if (written < 0 || (sz)written >= sizeof(next_boundary_marker)) {
            return 0;
        }
    }
    part_boundary = sw_find_multipart_boundary(hm->body, body_len, *offset, boundary_marker, strlen(boundary_marker), 0);
    if (part_boundary == NULL) {
        return 0;
    }

    part_offset = (sz)(part_boundary - hm->body) + strlen(boundary_marker);
    if (part_offset + 2 <= body_len && hm->body[part_offset] == '-' && hm->body[part_offset + 1] == '-') {
        return 0;
    }
    if (part_offset + 2 > body_len || hm->body[part_offset] != '\r' || hm->body[part_offset + 1] != '\n') {
        return 0;
    }
    part_offset += 2;

    headers_end_offset = sw_find_bytes_from(hm->body, body_len, part_offset, "\r\n\r\n", 4);
    if (headers_end_offset == SIZE_MAX) {
        return 0;
    }
    headers_end = hm->body + headers_end_offset;
    body_start = headers_end + 4;
    next_boundary = sw_find_multipart_boundary(
        hm->body,
        body_len,
        (sz)(body_start - hm->body),
        next_boundary_marker + 2,
        strlen(boundary_marker),
        1
    );
    if (next_boundary == NULL) {
        return 0;
    }

    memset(mp, 0, sizeof(*mp));
    mp->boundary = sw_strdup_cstr(boundary);
    if (mp->boundary == NULL) {
        goto fail;
    }
    mp->data = body_start;
    mp->data_len = (sz)(next_boundary - body_start);
    if (mp->data_len >= 2 && next_boundary[-2] == '\r' && next_boundary[-1] == '\n') {
        mp->data_len -= 2;
    }

    line_offset = part_offset;
    while (line_offset < (sz)(headers_end - hm->body)) {
        const sz line_end = sw_find_crlf_from(hm->body, (sz)(headers_end - hm->body) + 2, line_offset);
        const c8* line = hm->body + line_offset;
        const sz line_len = (line_end == SIZE_MAX) ? 0 : (line_end - line_offset);

        if (line_end == SIZE_MAX || line_len == 0) {
            break;
        }

        if (sw_header_value_matches_name(line, line_len, "Content-Disposition")) {
            mp->name = sw_parse_disposition_param(line, line_len, "name");
            if (mp->name == NULL) {
                goto fail;
            }
            mp->filename = sw_parse_disposition_param(line, line_len, "filename");
        } else if (sw_header_value_matches_name(line, line_len, "Content-Type")) {
            const c8* colon = memchr(line, ':', line_len);
            if (colon != NULL) {
                mp->content_type = sw_strdup_trimmed_range(colon + 1, line_len - (sz)(colon + 1 - line));
                if (mp->content_type == NULL) {
                    goto fail;
                }
            }
        }

        line_offset = line_end + 2;
    }

    *offset = (sz)(next_boundary - hm->body);
    return 1;

fail:
    sw_http_multipart_clear(mp);
    return 0;
}

i32 sw_http_multipart_save(const sw_http_multipart* mp, const c8* path) {
    FILE* out;
    b8 ok;

    if (mp == NULL || path == NULL) {
        return -1;
    }

    out = fopen(path, "wb");
    if (out == NULL) {
        return -1;
    }

    ok = mp->data != NULL && fwrite(mp->data, 1, mp->data_len, out) == mp->data_len;

    if (fclose(out) != 0) {
        ok = 0;
    }
    return ok ? 0 : -1;
}

typedef struct {
    sw_connection* connection;
    sz remaining;
    sz buffered_used;
    c8 socket_buffer[16384];
    sz socket_pos;
    sz socket_len;
    f64 deadline_ms;
} sw_upload_reader;

static void sw_upload_reader_consume_buffered(sw_upload_reader* reader) {
    if (reader != NULL && reader->buffered_used > 0) {
        sw_char_array_consume_prefix(&reader->connection->read_buffer, reader->buffered_used);
        reader->buffered_used = 0;
    }
}

static int sw_upload_reader_read(sw_upload_reader* reader, c8* out) {
    if (reader == NULL || reader->connection == NULL || out == NULL || reader->remaining == 0) {
        return 0;
    }

    if (reader->buffered_used < sw_char_array_size(&reader->connection->read_buffer)) {
        *out = sw_char_array_data(&reader->connection->read_buffer)[reader->buffered_used++];
        reader->remaining -= 1;
        if (reader->buffered_used >= 16384) {
            sw_upload_reader_consume_buffered(reader);
        }
        return 1;
    }
    sw_upload_reader_consume_buffered(reader);

    for (;;) {
        int read_bytes = 0;
        const sz to_read = reader->remaining < sizeof(reader->socket_buffer)
            ? reader->remaining
            : sizeof(reader->socket_buffer);

        if (reader->socket_pos < reader->socket_len) {
            *out = reader->socket_buffer[reader->socket_pos++];
            reader->remaining -= 1;
            return 1;
        }

#if defined(SYPHAX_WEB_HAS_TLS)
        {
            const int read_rc = sw_connection_transport_recv(
                reader->connection,
                reader->socket_buffer,
                to_read,
                &read_bytes
            );
            if (read_rc < 0) {
                return -1;
            }
            if (read_rc == 0) {
                read_bytes = 0;
            }
        }
#else
        read_bytes = (int)recv(reader->connection->fd, reader->socket_buffer, (int)to_read, 0);
        if (read_bytes < 0 && !sw_socket_error_is_would_block(sw_socket_last_error())) {
            return -1;
        }
        if (read_bytes == 0) {
            return -1;
        }
        if (read_bytes < 0) {
            read_bytes = 0;
        }
#endif

        if (read_bytes > 0) {
            reader->socket_pos = 0;
            reader->socket_len = (sz)read_bytes;
            sw_connection_mark_activity(reader->connection, sw_now_ms());
            continue;
        }

        if (sw_now_ms() >= reader->deadline_ms) {
            return -1;
        }
        s_thread_sleep_ms(1);
    }
}

static int sw_upload_reader_line(sw_upload_reader* reader, sw_char_array* line, sz max_len) {
    if (reader == NULL || line == NULL) {
        return -1;
    }
    sw_char_array_reset(line);

    for (;;) {
        c8 ch;
        const int rc = sw_upload_reader_read(reader, &ch);
        if (rc <= 0) {
            return -1;
        }
        if (ch == '\r') {
            c8 lf;
            if (sw_upload_reader_read(reader, &lf) <= 0 || lf != '\n') {
                return -1;
            }
            return 1;
        }
        if (!sw_char_array_append_byte(line, ch)) {
            return -1;
        }
        if (sw_char_array_size(line) > max_len) {
            return -1;
        }
    }
}

static b8 sw_upload_marker_prefix(const c8* marker, sz marker_len, const c8* pending, sz pending_len) {
    return pending_len <= marker_len && memcmp(marker, pending, pending_len) == 0;
}

static int sw_upload_emit_part_bytes(FILE* out, const c8* data, sz data_len, sz* out_size) {
    if (data_len == 0) {
        return 0;
    }
    if (out != NULL && fwrite(data, 1, data_len, out) != data_len) {
        return -1;
    }
    if (out_size != NULL) {
        *out_size += data_len;
    }
    return 0;
}

static int sw_upload_read_part_data(
    sw_upload_reader* reader,
    const c8* marker,
    sz marker_len,
    FILE* out,
    sz* out_size
) {
    c8 pending[512];
    sz pending_len = 0;

    if (reader == NULL || marker == NULL || marker_len == 0 || marker_len > sizeof(pending)) {
        return -1;
    }

    while (reader->remaining > 0) {
        c8 ch;
        const int rc = sw_upload_reader_read(reader, &ch);
        if (rc <= 0) {
            return -1;
        }

        pending[pending_len++] = ch;
        while (pending_len > 0 && !sw_upload_marker_prefix(marker, marker_len, pending, pending_len)) {
            if (sw_upload_emit_part_bytes(out, pending, 1, out_size) != 0) {
                return -1;
            }
            memmove(pending, pending + 1, pending_len - 1);
            pending_len -= 1;
        }

        if (pending_len == marker_len) {
            return 1;
        }
    }

    return -1;
}

i32 sw_http_upload_save(sw_connection* connection, const sw_http_message* hm, const c8* name, const c8* path, sz* out_size) {
    char boundary[256];
    char boundary_line[260];
    char data_marker[262];
    sw_upload_reader reader;
    sw_char_array line;
    sw_http_multipart part;
    FILE* out = NULL;
    b8 saved = 0;
    sz saved_size = 0;
    int rc = -1;

    if (out_size != NULL) {
        *out_size = 0;
    }
    if (connection == NULL || hm == NULL || path == NULL) {
        return -1;
    }

    if (!hm->body_pending) {
        sz offset = 0;
        memset(&part, 0, sizeof(part));
        while (sw_http_next_multipart(hm, &part, &offset)) {
            if (part.filename != NULL && (name == NULL || (part.name != NULL && strcmp(part.name, name) == 0))) {
                rc = sw_http_multipart_save(&part, path);
                if (rc == 0 && out_size != NULL) {
                    *out_size = part.data_len;
                }
                sw_http_multipart_clear(&part);
                return rc;
            }
            sw_http_multipart_clear(&part);
            memset(&part, 0, sizeof(part));
        }
        return -1;
    }

    if (&connection->request != hm
        || !sw_http_content_type_boundary(sw_http_header_get(hm, "Content-Type"), boundary, sizeof(boundary))) {
        return -1;
    }
    if (snprintf(boundary_line, sizeof(boundary_line), "--%s", boundary) < 0
        || strlen(boundary_line) >= sizeof(boundary_line)
        || snprintf(data_marker, sizeof(data_marker), "\r\n--%s", boundary) < 0
        || strlen(data_marker) >= sizeof(data_marker)) {
        return -1;
    }

    memset(&reader, 0, sizeof(reader));
    reader.connection = connection;
    reader.remaining = hm->content_length;
    reader.deadline_ms = sw_now_ms() + (f64)(connection->mgr != NULL && connection->mgr->config.body_timeout_ms > 0
        ? connection->mgr->config.body_timeout_ms
        : 120000);

    sw_char_array_init(&line);
    memset(&part, 0, sizeof(part));

    if (sw_upload_reader_line(&reader, &line, 512) <= 0
        || strcmp(sw_char_array_data(&line), boundary_line) != 0) {
        goto done;
    }

    for (;;) {
        b8 target_part = 0;
        sz part_size = 0;

        sw_http_multipart_clear(&part);
        memset(&part, 0, sizeof(part));

        for (;;) {
            const c8* header_line;
            const sz header_len_limit = connection->mgr != NULL ? connection->mgr->config.max_header_bytes : 16 * 1024;
            if (sw_upload_reader_line(&reader, &line, header_len_limit) <= 0) {
                goto done;
            }
            if (sw_char_array_size(&line) == 0) {
                break;
            }
            header_line = sw_char_array_data(&line);
            if (sw_header_value_matches_name(header_line, sw_char_array_size(&line), "Content-Disposition")) {
                part.name = sw_parse_disposition_param(header_line, sw_char_array_size(&line), "name");
                part.filename = sw_parse_disposition_param(header_line, sw_char_array_size(&line), "filename");
            } else if (sw_header_value_matches_name(header_line, sw_char_array_size(&line), "Content-Type")) {
                const c8* colon = memchr(header_line, ':', sw_char_array_size(&line));
                if (colon != NULL) {
                    part.content_type = sw_strdup_trimmed_range(
                        colon + 1,
                        sw_char_array_size(&line) - (sz)(colon + 1 - header_line)
                    );
                }
            }
        }

        target_part = !saved
            && part.filename != NULL
            && (name == NULL || (part.name != NULL && strcmp(part.name, name) == 0));
        if (target_part) {
            out = fopen(path, "wb");
            if (out == NULL) {
                goto done;
            }
        }

        if (sw_upload_read_part_data(&reader, data_marker, strlen(data_marker), out, target_part ? &part_size : NULL) <= 0) {
            goto done;
        }
        if (out != NULL) {
            if (fclose(out) != 0) {
                out = NULL;
                goto done;
            }
            out = NULL;
            saved = 1;
            saved_size = part_size;
        }

        if (sw_upload_reader_line(&reader, &line, 512) <= 0) {
            goto done;
        }
        if (strcmp(sw_char_array_data(&line), "--") == 0) {
            while (reader.remaining > 0) {
                c8 ignored;
                if (sw_upload_reader_read(&reader, &ignored) <= 0) {
                    goto done;
                }
            }
            rc = saved ? 0 : -1;
            break;
        }
        if (sw_char_array_size(&line) != 0) {
            goto done;
        }
    }

done:
    if (out != NULL) {
        fclose(out);
        remove(path);
    }
    sw_upload_reader_consume_buffered(&reader);
    sw_http_multipart_clear(&part);
    sw_char_array_free(&line);
    if (rc == 0) {
        connection->request.body_pending = 0;
        connection->must_close = 0;
        if (out_size != NULL) {
            *out_size = saved_size;
        }
    }
    return rc;
}

void sw_http_multipart_clear(sw_http_multipart* mp) {
    if (mp == NULL) {
        return;
    }
    free((void*)mp->boundary);
    free(mp->name);
    free(mp->filename);
    free(mp->content_type);
    memset(mp, 0, sizeof(*mp));
}

static i32 sw_random_bytes(u8* out, sz out_len) {
    sz done = 0;

    if (out == NULL && out_len > 0) {
        return -1;
    }
    if (out_len == 0) {
        return 0;
    }

#ifdef _WIN32
    return BCryptGenRandom(NULL, out, (ULONG)out_len, BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0 ? 0 : -1;
#else
    {
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0) {
            return -1;
        }
        while (done < out_len) {
            const ssize_t rc = read(fd, out + done, out_len - done);
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                close(fd);
                return -1;
            }
            if (rc == 0) {
                close(fd);
                return -1;
            }
            done += (sz)rc;
        }
        close(fd);
        return 0;
    }
#endif
}

static b8 sw_session_id_is_valid(const c8* id) {
    sz i;

    if (id == NULL || strlen(id) != 64) {
        return 0;
    }
    for (i = 0; i < 64; ++i) {
        if (!isxdigit((unsigned char)id[i])) {
            return 0;
        }
    }
    return 1;
}

static b8 sw_session_generate_id(c8 out[65]) {
    static const c8 hex[] = "0123456789abcdef";
    u8 bytes[32];
    sz i;

    if (out == NULL || sw_random_bytes(bytes, sizeof(bytes)) != 0) {
        return 0;
    }
    for (i = 0; i < sizeof(bytes); ++i) {
        out[i * 2] = hex[(bytes[i] >> 4) & 0x0f];
        out[i * 2 + 1] = hex[bytes[i] & 0x0f];
    }
    out[64] = '\0';
    return 1;
}

#if defined(SYPHAX_WEB_HAS_CRYPTO)
static c8* sw_base64url_encode(const u8* data, sz data_len) {
    c8* encoded;
    int encoded_len;
    sz i;

    if (data == NULL || data_len > (sz)INT_MAX) {
        return NULL;
    }

    encoded_len = 4 * (int)((data_len + 2) / 3);
    encoded = (c8*)calloc((sz)encoded_len + 1, 1);
    if (encoded == NULL) {
        return NULL;
    }
    if (EVP_EncodeBlock((unsigned char*)encoded, data, (int)data_len) != encoded_len) {
        free(encoded);
        return NULL;
    }

    for (i = 0; i < (sz)encoded_len; ++i) {
        if (encoded[i] == '+') {
            encoded[i] = '-';
        } else if (encoded[i] == '/') {
            encoded[i] = '_';
        }
    }
    while (encoded_len > 0 && encoded[encoded_len - 1] == '=') {
        encoded[--encoded_len] = '\0';
    }
    return encoded;
}

static u8* sw_base64url_decode(const c8* text, sz text_len, sz* out_len) {
    const sz mod = text_len % 4;
    const sz pad = (mod == 0) ? 0 : 4 - mod;
    const sz padded_len = text_len + pad;
    c8* base64;
    u8* decoded;
    int decoded_len;
    sz i;

    if (out_len != NULL) {
        *out_len = 0;
    }
    if (text == NULL || out_len == NULL || mod == 1 || padded_len > (sz)INT_MAX) {
        return NULL;
    }

    base64 = (c8*)calloc(padded_len + 1, 1);
    decoded = (u8*)calloc((padded_len / 4) * 3 + 1, 1);
    if (base64 == NULL || decoded == NULL) {
        free(base64);
        free(decoded);
        return NULL;
    }

    for (i = 0; i < text_len; ++i) {
        const c8 ch = text[i];
        if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            base64[i] = ch;
        } else if (ch == '-') {
            base64[i] = '+';
        } else if (ch == '_') {
            base64[i] = '/';
        } else {
            free(base64);
            free(decoded);
            return NULL;
        }
    }
    for (i = 0; i < pad; ++i) {
        base64[text_len + i] = '=';
    }

    decoded_len = EVP_DecodeBlock(decoded, (const unsigned char*)base64, (int)padded_len);
    free(base64);
    if (decoded_len < 0 || (sz)decoded_len < pad) {
        free(decoded);
        return NULL;
    }

    *out_len = (sz)decoded_len - pad;
    return decoded;
}

static b8 sw_token_encrypt_cookie_value(const sw_tokens* tokens, const c8 id[65], c8** out_value) {
    enum { SW_TOKEN_NONCE_LEN = 12, SW_TOKEN_TAG_LEN = 16, SW_TOKEN_ID_LEN = 64 };
    EVP_CIPHER_CTX* ctx = NULL;
    u8 nonce[SW_TOKEN_NONCE_LEN];
    u8 ciphertext[SW_TOKEN_ID_LEN];
    u8 tag[SW_TOKEN_TAG_LEN];
    u8 combined[SW_TOKEN_NONCE_LEN + SW_TOKEN_ID_LEN + SW_TOKEN_TAG_LEN];
    c8* encoded = NULL;
    c8* value = NULL;
    int len = 0;
    int ciphertext_len = 0;
    b8 ok = 0;

    if (out_value != NULL) {
        *out_value = NULL;
    }
    if (tokens == NULL || id == NULL || out_value == NULL || !sw_session_id_is_valid(id)) {
        return 0;
    }
    if (OPENSSL_init_crypto(0, NULL) != 1 || sw_random_bytes(nonce, sizeof(nonce)) != 0) {
        return 0;
    }

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        return 0;
    }
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, (int)sizeof(nonce), NULL) != 1
        || EVP_EncryptInit_ex(ctx, NULL, NULL, tokens->secret, nonce) != 1
        || EVP_EncryptUpdate(ctx, ciphertext, &len, (const unsigned char*)id, SW_TOKEN_ID_LEN) != 1) {
        goto done;
    }
    ciphertext_len = len;
    if (EVP_EncryptFinal_ex(ctx, ciphertext + ciphertext_len, &len) != 1) {
        goto done;
    }
    ciphertext_len += len;
    if (ciphertext_len != SW_TOKEN_ID_LEN
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, (int)sizeof(tag), tag) != 1) {
        goto done;
    }

    memcpy(combined, nonce, sizeof(nonce));
    memcpy(combined + sizeof(nonce), ciphertext, sizeof(ciphertext));
    memcpy(combined + sizeof(nonce) + sizeof(ciphertext), tag, sizeof(tag));
    encoded = sw_base64url_encode(combined, sizeof(combined));
    if (encoded == NULL) {
        goto done;
    }

    value = (c8*)calloc(strlen(encoded) + 4, 1);
    if (value == NULL) {
        goto done;
    }
    memcpy(value, "v1.", 3);
    strcpy(value + 3, encoded);
    *out_value = value;
    value = NULL;
    ok = 1;

done:
    EVP_CIPHER_CTX_free(ctx);
    free(encoded);
    free(value);
    return ok;
}

static b8 sw_token_decrypt_cookie_value(const sw_tokens* tokens, const c8* value, c8 out_id[65]) {
    enum { SW_TOKEN_NONCE_LEN = 12, SW_TOKEN_TAG_LEN = 16, SW_TOKEN_ID_LEN = 64 };
    EVP_CIPHER_CTX* ctx = NULL;
    u8* decoded = NULL;
    sz decoded_len = 0;
    const u8* nonce;
    const u8* ciphertext;
    const u8* tag;
    u8 plaintext[SW_TOKEN_ID_LEN + 1];
    int len = 0;
    int plaintext_len = 0;
    b8 ok = 0;

    if (out_id != NULL) {
        out_id[0] = '\0';
    }
    if (tokens == NULL || value == NULL || out_id == NULL || strncmp(value, "v1.", 3) != 0) {
        return 0;
    }

    decoded = sw_base64url_decode(value + 3, strlen(value + 3), &decoded_len);
    if (decoded == NULL || decoded_len != SW_TOKEN_NONCE_LEN + SW_TOKEN_ID_LEN + SW_TOKEN_TAG_LEN) {
        goto done;
    }

    nonce = decoded;
    ciphertext = decoded + SW_TOKEN_NONCE_LEN;
    tag = decoded + SW_TOKEN_NONCE_LEN + SW_TOKEN_ID_LEN;

    ctx = EVP_CIPHER_CTX_new();
    if (ctx == NULL) {
        goto done;
    }
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1
        || EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, SW_TOKEN_NONCE_LEN, NULL) != 1
        || EVP_DecryptInit_ex(ctx, NULL, NULL, tokens->secret, nonce) != 1
        || EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, SW_TOKEN_ID_LEN) != 1) {
        goto done;
    }
    plaintext_len = len;
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, SW_TOKEN_TAG_LEN, (void*)tag) != 1
        || EVP_DecryptFinal_ex(ctx, plaintext + plaintext_len, &len) != 1) {
        goto done;
    }
    plaintext_len += len;
    if (plaintext_len != SW_TOKEN_ID_LEN) {
        goto done;
    }
    plaintext[SW_TOKEN_ID_LEN] = '\0';
    memcpy(out_id, plaintext, SW_TOKEN_ID_LEN + 1);
    ok = sw_session_id_is_valid(out_id);

done:
    EVP_CIPHER_CTX_free(ctx);
    free(decoded);
    return ok;
}
#endif

static void sw_session_free(sw_session* session) {
    sz i;

    if (session == NULL) {
        return;
    }
    for (i = 0; i < s_array_get_size(&session->items); ++i) {
        sw_session_item* item = &s_array_get_data(&session->items)[i];
        free(item->key);
        free(item->value);
    }
    s_array_clear(&session->items);
    memset(session, 0, sizeof(*session));
}

static void sw_sessions_remove_at(sw_sessions* sessions, sz index) {
    sw_session* session;
    s_handle handle;

    if (sessions == NULL || index >= s_array_get_size(&sessions->sessions)) {
        return;
    }
    handle = s_array_handle(&sessions->sessions, (u32)index);
    session = s_array_get(&sessions->sessions, handle);
    sw_session_free(session);
    s_array_remove_ordered(&sessions->sessions, handle);
}

static void sw_sessions_cleanup_expired(sw_sessions* sessions, f64 now_ms) {
    sz i = 0;

    if (sessions == NULL) {
        return;
    }
    while (i < s_array_get_size(&sessions->sessions)) {
        sw_session* session = &s_array_get_data(&sessions->sessions)[i];
        if (session->expires_at_ms <= now_ms) {
            sw_sessions_remove_at(sessions, i);
            continue;
        }
        ++i;
    }
}

static sw_session* sw_sessions_find_id(sw_sessions* sessions, const c8* id, f64 now_ms) {
    sz i;

    if (sessions == NULL || !sw_session_id_is_valid(id)) {
        return NULL;
    }
    sw_sessions_cleanup_expired(sessions, now_ms);
    for (i = 0; i < s_array_get_size(&sessions->sessions); ++i) {
        sw_session* session = &s_array_get_data(&sessions->sessions)[i];
        if (strcmp(session->id, id) == 0) {
            return session;
        }
    }
    return NULL;
}

static void sw_sessions_evict_oldest(sw_sessions* sessions) {
    sz oldest_index = 0;
    sz i;
    f64 oldest_touched;

    if (sessions == NULL || s_array_get_size(&sessions->sessions) == 0) {
        return;
    }

    oldest_touched = s_array_get_data(&sessions->sessions)[0].touched_at_ms;
    for (i = 1; i < s_array_get_size(&sessions->sessions); ++i) {
        sw_session* session = &s_array_get_data(&sessions->sessions)[i];
        if (session->touched_at_ms < oldest_touched) {
            oldest_touched = session->touched_at_ms;
            oldest_index = i;
        }
    }
    sw_sessions_remove_at(sessions, oldest_index);
}

static b8 sw_session_cookie_has_values(const sw_http_cookie* cookie) {
    return cookie != NULL
        && (cookie->path != NULL
            || cookie->domain != NULL
            || cookie->expires != NULL
            || cookie->max_age != 0
            || cookie->http_only
            || cookie->secure
            || cookie->secure_auto
            || cookie->same_site != SW_COOKIE_SAMESITE_UNSET);
}

static b8 sw_sessions_copy_cookie(sw_http_cookie* out, const sw_http_cookie* source) {
    if (out == NULL || source == NULL) {
        return 0;
    }

    *out = *source;
    out->path = source->path != NULL ? sw_strdup_cstr(source->path) : NULL;
    out->domain = source->domain != NULL ? sw_strdup_cstr(source->domain) : NULL;
    out->expires = source->expires != NULL ? sw_strdup_cstr(source->expires) : NULL;

    if ((source->path != NULL && out->path == NULL)
        || (source->domain != NULL && out->domain == NULL)
        || (source->expires != NULL && out->expires == NULL)) {
        free((void*)out->path);
        free((void*)out->domain);
        free((void*)out->expires);
        memset(out, 0, sizeof(*out));
        return 0;
    }
    return 1;
}

static void sw_sessions_free_cookie(sw_http_cookie* cookie) {
    if (cookie == NULL) {
        return;
    }
    free((void*)cookie->path);
    free((void*)cookie->domain);
    free((void*)cookie->expires);
    memset(cookie, 0, sizeof(*cookie));
}

sw_session_config sw_session_config_default(void) {
    sw_session_config config;

    memset(&config, 0, sizeof(config));
    config.cookie_name = "sw_session";
    config.ttl_seconds = 3600;
    config.max_sessions = 1024;
    config.max_items = 32;
    config.cookie = sw_http_cookie_default();
    config.cookie.max_age = -1;
    return config;
}

sw_sessions* sw_sessions_create(const sw_session_config* config) {
    sw_session_config effective = sw_session_config_default();
    sw_sessions* sessions;
    b8 explicit_cookie = 0;

    if (config != NULL) {
        if (config->cookie_name != NULL) {
            effective.cookie_name = config->cookie_name;
        }
        if (config->ttl_seconds > 0) {
            effective.ttl_seconds = config->ttl_seconds;
        }
        if (config->max_sessions > 0) {
            effective.max_sessions = config->max_sessions;
        }
        if (config->max_items > 0) {
            effective.max_items = config->max_items;
        }
        if (sw_session_cookie_has_values(&config->cookie)) {
            effective.cookie = config->cookie;
            explicit_cookie = 1;
        }
    }
    if (!explicit_cookie || effective.cookie.max_age <= 0) {
        effective.cookie.max_age = effective.ttl_seconds;
    }

    sessions = (sw_sessions*)calloc(1, sizeof(*sessions));
    if (sessions == NULL) {
        return NULL;
    }
    sessions->cookie_name = sw_strdup_cstr(effective.cookie_name);
    sessions->ttl_seconds = effective.ttl_seconds;
    sessions->max_sessions = effective.max_sessions;
    sessions->max_items = effective.max_items;
    s_array_init(&sessions->sessions);
    if (sessions->cookie_name == NULL || !sw_sessions_copy_cookie(&sessions->cookie, &effective.cookie)) {
        sw_sessions_destroy(sessions);
        return NULL;
    }
    return sessions;
}

void sw_sessions_destroy(sw_sessions* sessions) {
    sz i;

    if (sessions == NULL) {
        return;
    }
    for (i = 0; i < s_array_get_size(&sessions->sessions); ++i) {
        sw_session_free(&s_array_get_data(&sessions->sessions)[i]);
    }
    s_array_clear(&sessions->sessions);
    free(sessions->cookie_name);
    sw_sessions_free_cookie(&sessions->cookie);
    free(sessions);
}

sw_session* sw_sessions_find(sw_sessions* sessions, const sw_http_message* hm) {
    c8 id[128];

    if (sessions == NULL || hm == NULL) {
        return NULL;
    }
    if (sw_http_get_cookie(hm, sessions->cookie_name, id, sizeof(id)) <= 0) {
        sw_sessions_cleanup_expired(sessions, sw_now_ms());
        return NULL;
    }
    return sw_sessions_find_id(sessions, id, sw_now_ms());
}

sw_session* sw_sessions_start(sw_sessions* sessions, sw_connection* connection, const sw_http_message* hm) {
    const f64 now_ms = sw_now_ms();
    sw_session* session = NULL;
    sw_session new_session;
    s_handle handle;
    int attempt;

    if (sessions == NULL || connection == NULL || hm == NULL) {
        return NULL;
    }

    session = sw_sessions_find(sessions, hm);
    if (session != NULL) {
        session->touched_at_ms = now_ms;
        session->expires_at_ms = now_ms + (f64)sessions->ttl_seconds * 1000.0;
        return sw_http_set_cookie(connection, sessions->cookie_name, session->id, &sessions->cookie) == 0 ? session : NULL;
    }

    sw_sessions_cleanup_expired(sessions, now_ms);
    while (s_array_get_size(&sessions->sessions) >= sessions->max_sessions && s_array_get_size(&sessions->sessions) > 0) {
        sw_sessions_evict_oldest(sessions);
    }

    memset(&new_session, 0, sizeof(new_session));
    s_array_init(&new_session.items);
    new_session.expires_at_ms = now_ms + (f64)sessions->ttl_seconds * 1000.0;
    new_session.touched_at_ms = now_ms;
    new_session.max_items = sessions->max_items;

    for (attempt = 0; attempt < 16; ++attempt) {
        if (!sw_session_generate_id(new_session.id)) {
            sw_session_free(&new_session);
            return NULL;
        }
        if (sw_sessions_find_id(sessions, new_session.id, now_ms) == NULL) {
            break;
        }
    }
    if (attempt == 16) {
        sw_session_free(&new_session);
        return NULL;
    }

    handle = s_array_add(&sessions->sessions, new_session);
    session = s_array_get(&sessions->sessions, handle);
    if (session == NULL || sw_http_set_cookie(connection, sessions->cookie_name, session->id, &sessions->cookie) != 0) {
        if (session != NULL) {
            sw_session_free(session);
            s_array_remove_ordered(&sessions->sessions, handle);
        }
        return NULL;
    }
    return session;
}

i32 sw_sessions_end(sw_sessions* sessions, sw_connection* connection, const sw_http_message* hm) {
    c8 id[128];
    sz i;

    if (sessions == NULL || connection == NULL) {
        return -1;
    }
    if (hm != NULL && sw_http_get_cookie(hm, sessions->cookie_name, id, sizeof(id)) > 0) {
        for (i = 0; i < s_array_get_size(&sessions->sessions); ++i) {
            sw_session* session = &s_array_get_data(&sessions->sessions)[i];
            if (strcmp(session->id, id) == 0) {
                sw_sessions_remove_at(sessions, i);
                break;
            }
        }
    }
    return sw_http_clear_cookie(connection, sessions->cookie_name, &sessions->cookie);
}

const c8* sw_session_id(const sw_session* session) {
    return (session != NULL) ? session->id : "";
}

const c8* sw_session_get(const sw_session* session, const c8* key) {
    sz i;

    if (session == NULL || key == NULL) {
        return NULL;
    }
    for (i = 0; i < s_array_get_size(&session->items); ++i) {
        const sw_session_item* item = &s_array_get_data((sw_session_item_array*)&session->items)[i];
        if (item->key != NULL && strcmp(item->key, key) == 0) {
            return item->value;
        }
    }
    return NULL;
}

i32 sw_session_set(sw_session* session, const c8* key, const c8* value) {
    sz i;
    sw_session_item item;

    if (session == NULL || key == NULL || *key == '\0' || value == NULL) {
        return -1;
    }
    for (i = 0; i < s_array_get_size(&session->items); ++i) {
        sw_session_item* existing = &s_array_get_data(&session->items)[i];
        if (existing->key != NULL && strcmp(existing->key, key) == 0) {
            c8* copy = sw_strdup_cstr(value);
            if (copy == NULL) {
                return -1;
            }
            free(existing->value);
            existing->value = copy;
            return 0;
        }
    }
    if (s_array_get_size(&session->items) >= session->max_items) {
        return -1;
    }
    item.key = sw_strdup_cstr(key);
    item.value = sw_strdup_cstr(value);
    if (item.key == NULL || item.value == NULL) {
        free(item.key);
        free(item.value);
        return -1;
    }
    s_array_add(&session->items, item);
    return 0;
}

i32 sw_session_remove(sw_session* session, const c8* key) {
    sz i;

    if (session == NULL || key == NULL) {
        return -1;
    }
    for (i = 0; i < s_array_get_size(&session->items); ++i) {
        sw_session_item* item = &s_array_get_data(&session->items)[i];
        if (item->key != NULL && strcmp(item->key, key) == 0) {
            const s_handle handle = s_array_handle(&session->items, (u32)i);
            free(item->key);
            free(item->value);
            s_array_remove_ordered(&session->items, handle);
            return 0;
        }
    }
    return 0;
}

static void sw_token_free(sw_token* token) {
    sz i;

    if (token == NULL) {
        return;
    }
    for (i = 0; i < s_array_get_size(&token->items); ++i) {
        sw_token_item* item = &s_array_get_data(&token->items)[i];
        free(item->key);
        free(item->value);
    }
    s_array_clear(&token->items);
    memset(token, 0, sizeof(*token));
}

#if defined(SYPHAX_WEB_HAS_CRYPTO)
static void sw_tokens_remove_at(sw_tokens* tokens, sz index) {
    sw_token* token;
    s_handle handle;

    if (tokens == NULL || index >= s_array_get_size(&tokens->tokens)) {
        return;
    }
    handle = s_array_handle(&tokens->tokens, (u32)index);
    token = s_array_get(&tokens->tokens, handle);
    sw_token_free(token);
    s_array_remove_ordered(&tokens->tokens, handle);
}

static void sw_tokens_cleanup_expired(sw_tokens* tokens, f64 now_ms) {
    sz i = 0;

    if (tokens == NULL) {
        return;
    }
    while (i < s_array_get_size(&tokens->tokens)) {
        sw_token* token = &s_array_get_data(&tokens->tokens)[i];
        if (token->expires_at_ms <= now_ms) {
            sw_tokens_remove_at(tokens, i);
            continue;
        }
        ++i;
    }
}

static sw_token* sw_tokens_find_id(sw_tokens* tokens, const c8* id, f64 now_ms) {
    sz i;

    if (tokens == NULL || !sw_session_id_is_valid(id)) {
        return NULL;
    }
    sw_tokens_cleanup_expired(tokens, now_ms);
    for (i = 0; i < s_array_get_size(&tokens->tokens); ++i) {
        sw_token* token = &s_array_get_data(&tokens->tokens)[i];
        if (strcmp(token->id, id) == 0) {
            return token;
        }
    }
    return NULL;
}

static void sw_tokens_evict_oldest(sw_tokens* tokens) {
    sz oldest_index = 0;
    sz i;
    f64 oldest_touched;

    if (tokens == NULL || s_array_get_size(&tokens->tokens) == 0) {
        return;
    }

    oldest_touched = s_array_get_data(&tokens->tokens)[0].touched_at_ms;
    for (i = 1; i < s_array_get_size(&tokens->tokens); ++i) {
        sw_token* token = &s_array_get_data(&tokens->tokens)[i];
        if (token->touched_at_ms < oldest_touched) {
            oldest_touched = token->touched_at_ms;
            oldest_index = i;
        }
    }
    sw_tokens_remove_at(tokens, oldest_index);
}

static i32 sw_tokens_cookie_id(sw_tokens* tokens, const sw_http_message* hm, c8 out_id[65]) {
    c8 cookie_value[512];
    const i32 cookie_len = (tokens != NULL && hm != NULL)
        ? sw_http_get_cookie(hm, tokens->cookie_name, cookie_value, sizeof(cookie_value))
        : -1;

    if (out_id != NULL) {
        out_id[0] = '\0';
    }
    if (tokens == NULL || hm == NULL || out_id == NULL) {
        return -1;
    }
    if (cookie_len <= 0) {
        return 0;
    }
    return sw_token_decrypt_cookie_value(tokens, cookie_value, out_id) ? 1 : -1;
}

static void sw_tokens_clear_cookie_if_possible(sw_tokens* tokens, sw_connection* connection) {
    if (tokens != NULL && connection != NULL) {
        (void)sw_http_clear_cookie(connection, tokens->cookie_name, &tokens->cookie);
    }
}
#endif

sw_token_config sw_token_config_default(void) {
    sw_token_config config;

    memset(&config, 0, sizeof(config));
    config.cookie_name = "sw_token";
    config.secret = NULL;
    config.secret_len = 0;
    config.ttl_seconds = 3600;
    config.max_tokens = 1024;
    config.max_items = 32;
    config.cookie = sw_http_cookie_default();
    config.cookie.max_age = -1;
    return config;
}

sw_tokens* sw_tokens_create(const sw_token_config* config) {
#if defined(SYPHAX_WEB_HAS_CRYPTO)
    sw_token_config effective = sw_token_config_default();
    sw_tokens* tokens;
    b8 explicit_cookie = 0;

    if (config != NULL) {
        if (config->cookie_name != NULL) {
            effective.cookie_name = config->cookie_name;
        }
        if (config->secret != NULL) {
            effective.secret = config->secret;
            effective.secret_len = config->secret_len;
        }
        if (config->ttl_seconds > 0) {
            effective.ttl_seconds = config->ttl_seconds;
        }
        if (config->max_tokens > 0) {
            effective.max_tokens = config->max_tokens;
        }
        if (config->max_items > 0) {
            effective.max_items = config->max_items;
        }
        if (sw_session_cookie_has_values(&config->cookie)) {
            effective.cookie = config->cookie;
            explicit_cookie = 1;
        }
    }
    if (effective.secret != NULL && effective.secret_len != 32) {
        return NULL;
    }
    if (!explicit_cookie || effective.cookie.max_age <= 0) {
        effective.cookie.max_age = effective.ttl_seconds;
    }

    tokens = (sw_tokens*)calloc(1, sizeof(*tokens));
    if (tokens == NULL) {
        return NULL;
    }
    tokens->cookie_name = sw_strdup_cstr(effective.cookie_name);
    tokens->ttl_seconds = effective.ttl_seconds;
    tokens->max_tokens = effective.max_tokens;
    tokens->max_items = effective.max_items;
    s_array_init(&tokens->tokens);
    if (tokens->cookie_name == NULL || !sw_sessions_copy_cookie(&tokens->cookie, &effective.cookie)) {
        sw_tokens_destroy(tokens);
        return NULL;
    }
    if (effective.secret != NULL) {
        memcpy(tokens->secret, effective.secret, sizeof(tokens->secret));
    } else if (sw_random_bytes(tokens->secret, sizeof(tokens->secret)) != 0) {
        sw_tokens_destroy(tokens);
        return NULL;
    }
    return tokens;
#else
    (void)config;
    return NULL;
#endif
}

void sw_tokens_destroy(sw_tokens* tokens) {
    sz i;

    if (tokens == NULL) {
        return;
    }
    for (i = 0; i < s_array_get_size(&tokens->tokens); ++i) {
        sw_token_free(&s_array_get_data(&tokens->tokens)[i]);
    }
    s_array_clear(&tokens->tokens);
    free(tokens->cookie_name);
    sw_sessions_free_cookie(&tokens->cookie);
    memset(tokens->secret, 0, sizeof(tokens->secret));
    free(tokens);
}

sw_token* sw_tokens_login(sw_tokens* tokens, sw_connection* connection, const sw_http_message* hm, const c8* user_id) {
#if defined(SYPHAX_WEB_HAS_CRYPTO)
    const f64 now_ms = sw_now_ms();
    sw_token new_token;
    sw_token* token = NULL;
    s_handle handle;
    c8 current_id[65];
    c8* cookie_value = NULL;
    int attempt;

    if (tokens == NULL || connection == NULL || hm == NULL) {
        return NULL;
    }

    if (sw_tokens_cookie_id(tokens, hm, current_id) == 1) {
        sz i;
        for (i = 0; i < s_array_get_size(&tokens->tokens); ++i) {
            sw_token* existing = &s_array_get_data(&tokens->tokens)[i];
            if (strcmp(existing->id, current_id) == 0) {
                sw_tokens_remove_at(tokens, i);
                break;
            }
        }
    }

    sw_tokens_cleanup_expired(tokens, now_ms);
    while (s_array_get_size(&tokens->tokens) >= tokens->max_tokens && s_array_get_size(&tokens->tokens) > 0) {
        sw_tokens_evict_oldest(tokens);
    }

    memset(&new_token, 0, sizeof(new_token));
    s_array_init(&new_token.items);
    new_token.expires_at_ms = now_ms + (f64)tokens->ttl_seconds * 1000.0;
    new_token.touched_at_ms = now_ms;
    new_token.max_items = tokens->max_items;

    for (attempt = 0; attempt < 16; ++attempt) {
        if (!sw_session_generate_id(new_token.id)) {
            sw_token_free(&new_token);
            return NULL;
        }
        if (sw_tokens_find_id(tokens, new_token.id, now_ms) == NULL) {
            break;
        }
    }
    if (attempt == 16) {
        sw_token_free(&new_token);
        return NULL;
    }

    handle = s_array_add(&tokens->tokens, new_token);
    token = s_array_get(&tokens->tokens, handle);
    if (token == NULL) {
        return NULL;
    }
    if (user_id != NULL && sw_token_set(token, "user_id", user_id) != 0) {
        sw_token_free(token);
        s_array_remove_ordered(&tokens->tokens, handle);
        return NULL;
    }
    if (!sw_token_encrypt_cookie_value(tokens, token->id, &cookie_value)
        || sw_http_set_cookie(connection, tokens->cookie_name, cookie_value, &tokens->cookie) != 0) {
        free(cookie_value);
        sw_token_free(token);
        s_array_remove_ordered(&tokens->tokens, handle);
        return NULL;
    }
    free(cookie_value);
    return token;
#else
    (void)tokens;
    (void)connection;
    (void)hm;
    (void)user_id;
    return NULL;
#endif
}

sw_token* sw_tokens_current(sw_tokens* tokens, sw_connection* connection, const sw_http_message* hm) {
#if defined(SYPHAX_WEB_HAS_CRYPTO)
    const f64 now_ms = sw_now_ms();
    c8 id[65];
    c8* cookie_value = NULL;
    sw_token* token;
    const i32 cookie_status = sw_tokens_cookie_id(tokens, hm, id);

    if (tokens == NULL || hm == NULL) {
        return NULL;
    }
    if (cookie_status == 0) {
        sw_tokens_cleanup_expired(tokens, now_ms);
        return NULL;
    }
    if (cookie_status < 0) {
        sw_tokens_clear_cookie_if_possible(tokens, connection);
        return NULL;
    }

    token = sw_tokens_find_id(tokens, id, now_ms);
    if (token == NULL) {
        sw_tokens_clear_cookie_if_possible(tokens, connection);
        return NULL;
    }
    token->touched_at_ms = now_ms;
    token->expires_at_ms = now_ms + (f64)tokens->ttl_seconds * 1000.0;
    if (connection != NULL) {
        if (!sw_token_encrypt_cookie_value(tokens, token->id, &cookie_value)
            || sw_http_set_cookie(connection, tokens->cookie_name, cookie_value, &tokens->cookie) != 0) {
            free(cookie_value);
            return NULL;
        }
        free(cookie_value);
    }
    return token;
#else
    (void)tokens;
    (void)connection;
    (void)hm;
    return NULL;
#endif
}

i32 sw_tokens_logout(sw_tokens* tokens, sw_connection* connection, const sw_http_message* hm) {
#if defined(SYPHAX_WEB_HAS_CRYPTO)
    c8 id[65];
    const i32 cookie_status = sw_tokens_cookie_id(tokens, hm, id);
    sz i;

    if (tokens == NULL || connection == NULL) {
        return -1;
    }
    if (cookie_status == 1) {
        for (i = 0; i < s_array_get_size(&tokens->tokens); ++i) {
            sw_token* token = &s_array_get_data(&tokens->tokens)[i];
            if (strcmp(token->id, id) == 0) {
                sw_tokens_remove_at(tokens, i);
                break;
            }
        }
    }
    return sw_http_clear_cookie(connection, tokens->cookie_name, &tokens->cookie);
#else
    (void)tokens;
    (void)connection;
    (void)hm;
    return -1;
#endif
}

const c8* sw_token_id(const sw_token* token) {
    return (token != NULL) ? token->id : "";
}

const c8* sw_token_get(const sw_token* token, const c8* key) {
    sz i;

    if (token == NULL || key == NULL) {
        return NULL;
    }
    for (i = 0; i < s_array_get_size(&token->items); ++i) {
        const sw_token_item* item = &s_array_get_data((sw_token_item_array*)&token->items)[i];
        if (item->key != NULL && strcmp(item->key, key) == 0) {
            return item->value;
        }
    }
    return NULL;
}

i32 sw_token_set(sw_token* token, const c8* key, const c8* value) {
    sz i;
    sw_token_item item;

    if (token == NULL || key == NULL || *key == '\0' || value == NULL) {
        return -1;
    }
    for (i = 0; i < s_array_get_size(&token->items); ++i) {
        sw_token_item* existing = &s_array_get_data(&token->items)[i];
        if (existing->key != NULL && strcmp(existing->key, key) == 0) {
            c8* copy = sw_strdup_cstr(value);
            if (copy == NULL) {
                return -1;
            }
            free(existing->value);
            existing->value = copy;
            return 0;
        }
    }
    if (s_array_get_size(&token->items) >= token->max_items) {
        return -1;
    }
    item.key = sw_strdup_cstr(key);
    item.value = sw_strdup_cstr(value);
    if (item.key == NULL || item.value == NULL) {
        free(item.key);
        free(item.value);
        return -1;
    }
    s_array_add(&token->items, item);
    return 0;
}

i32 sw_token_remove(sw_token* token, const c8* key) {
    sz i;

    if (token == NULL || key == NULL) {
        return -1;
    }
    for (i = 0; i < s_array_get_size(&token->items); ++i) {
        sw_token_item* item = &s_array_get_data(&token->items)[i];
        if (item->key != NULL && strcmp(item->key, key) == 0) {
            const s_handle handle = s_array_handle(&token->items, (u32)i);
            free(item->key);
            free(item->value);
            s_array_remove_ordered(&token->items, handle);
            return 0;
        }
    }
    return 0;
}

const c8* sw_connection_remote_ip(const sw_connection* connection) {
    if (connection == NULL || connection->remote_ip[0] == '\0') {
        return "";
    }
    return connection->remote_ip;
}

u16 sw_connection_remote_port(const sw_connection* connection) {
    return (connection != NULL) ? connection->remote_port : 0;
}

b8 sw_connection_is_secure(const sw_connection* connection) {
    return connection != NULL && connection->secure;
}

const c8* sw_connection_alpn(const sw_connection* connection) {
    if (connection == NULL) {
        return "";
    }
    return connection->alpn;
}

void* sw_connection_user_data(sw_connection* connection) {
    return (connection != NULL) ? connection->user_data : NULL;
}

void sw_connection_set_user_data(sw_connection* connection, void* user_data) {
    if (connection != NULL) {
        connection->user_data = user_data;
    }
}
