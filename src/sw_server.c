#include "sw_backend.h"
#include "sw_utility.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#if !defined(_WIN32)
#    include <signal.h>
#endif

#if defined(SYPHAX_WEB_HAS_TLS)
#    include <openssl/ssl.h>
#endif

typedef enum {
    SW_PARSE_PENDING = 0,
    SW_PARSE_READY = 1,
    SW_PARSE_BAD_REQUEST = -1,
    SW_PARSE_UNSUPPORTED_CHUNKED = -2,
    SW_PARSE_HEADERS_TOO_LARGE = -3,
    SW_PARSE_PAYLOAD_TOO_LARGE = -4,
    SW_PARSE_TOO_MANY_HEADERS = -5
} sw_parse_result;

#if defined(SYPHAX_WEB_HAS_TLS)
typedef enum {
    SW_TLS_OP_HANDSHAKE = 0,
    SW_TLS_OP_READ = 1,
    SW_TLS_OP_WRITE = 2
} sw_tls_operation;
#endif

static sz sw_socket_runtime_users = 0;
static sw_mgr* sw_signal_mgr = NULL;

static sz sw_http_config_max_request_bytes(const sw_http_config* config);
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
static i32 sw_mgr_effective_poll_timeout(const sw_mgr* mgr, i32 timeout_ms);
static void sw_mgr_enforce_timeouts(sw_mgr* mgr);
static i32 sw_http_reply_status_text(sw_connection* connection, i32 status_code, const c8* body);
static b8 sw_path_join(char* out, sz out_cap, const c8* lhs, const c8* rhs);
static b8 sw_path_real(const c8* path, char* out, sz out_cap);
static b8 sw_path_has_prefix(const c8* path, const c8* prefix);
static b8 sw_decode_request_path(const c8* request_path, char* out, sz out_cap);
static sz sw_http_path_length(const c8* uri);
static int sw_hex_value(c8 ch);
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

#if defined(SYPHAX_WEB_HAS_TLS)
static SSL_CTX* sw_tls_context_create(const sw_tls_config* config);
static void sw_tls_context_free(SSL_CTX* ctx);
static int sw_connection_tls_handshake(sw_mgr* mgr, sw_connection* connection);
static b8 sw_connection_tls_handshake_pending(const sw_connection* connection);
static int sw_connection_transport_recv(sw_connection* connection, c8* data, sz data_cap, int* out_read);
static int sw_connection_transport_send(sw_connection* connection, const c8* data, sz data_len, int* out_sent);
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
        sw_connection_mark_activity(connection, sw_get_time());
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

static sz sw_http_config_max_request_bytes(const sw_http_config* config) {
    sz total = 4;

    if (config == NULL) {
        return SIZE_MAX;
    }
    if (config->max_header_bytes > SIZE_MAX - total) {
        return SIZE_MAX;
    }
    total += config->max_header_bytes;
    if (config->max_body_bytes > SIZE_MAX - total) {
        return SIZE_MAX;
    }
    return total + config->max_body_bytes;
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

static sw_socket sw_create_listener_socket(const c8* host, const c8* port, u16* out_bound_port) {
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
            && listen(listener_fd, 128) == 0
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

static sw_connection* sw_connection_create(sw_mgr* mgr, sw_listener* listener, sw_socket fd, const c8* remote_ip, u16 remote_port) {
    sw_connection* connection = (sw_connection*)calloc(1, sizeof(*connection));
    s_handle handle;
    const f64 now_ms = sw_get_time();

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
    s_array_init(&connection->header_storage);

    handle = s_array_add(&mgr->connections, connection);
    connection->array_handle = handle;

    if (sw_backend_register_connection(mgr, connection) != 0) {
        s_array_remove_ordered(&mgr->connections, handle);
#if defined(SYPHAX_WEB_HAS_TLS)
        if (connection->tls != NULL) {
            SSL_free(connection->tls);
        }
#endif
        sw_char_array_free(&connection->read_buffer);
        sw_char_array_free(&connection->write_buffer);
        s_array_clear(&connection->header_storage);
        free(connection);
        return NULL;
    }

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
    connection->headers_complete = 0;
    connection->phase_started_at_ms = sw_get_time();
}

static void sw_connection_mark_activity(sw_connection* connection, f64 now_ms) {
    if (connection != NULL) {
        connection->last_activity_at_ms = now_ms;
    }
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
    sw_char_array_free(&connection->read_buffer);
    sw_char_array_free(&connection->write_buffer);

    if (connection->array_handle != S_HANDLE_NULL) {
        s_array_remove_ordered(&mgr->connections, connection->array_handle);
    }

    free(connection);
}

int sw_mgr_sync_connection(sw_mgr* mgr, sw_connection* connection) {
    if (mgr == NULL || connection == NULL) {
        return -1;
    }
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
        sw_connection_mark_activity(connection, sw_get_time());
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

static sw_parse_result sw_connection_try_parse_request(sw_connection* connection) {
    const sw_http_config* config;
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
        connection->headers_complete = 1;
        connection->phase_started_at_ms = sw_get_time();
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
            if (sw_strcasestr_ascii(value, "chunked") != NULL) {
                parsed_request.is_chunked = 1;
            }
        }

        cursor = line_end + 2;
    }

    if (parsed_request.is_chunked) {
        goto unsupported_chunked;
    }
    if (parsed_request.content_length > config->max_body_bytes) {
        goto payload_too_large;
    }
    if (parsed_request.content_length > SIZE_MAX - (header_end + 4)) {
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
    parsed_request.headers = s_array_get_data(&parsed_headers);
    parsed_request.num_headers = s_array_get_size(&parsed_headers);

    connection->header_storage = parsed_headers;
    connection->request = parsed_request;
    connection->request_ready = 1;
    return SW_PARSE_READY;

pending:
    free((void*)parsed_request.method);
    free((void*)parsed_request.uri);
    free((void*)parsed_request.proto);
    sw_http_header_array_free_owned(&parsed_headers);
    return SW_PARSE_PENDING;

unsupported_chunked:
    free((void*)parsed_request.method);
    free((void*)parsed_request.uri);
    free((void*)parsed_request.proto);
    sw_http_header_array_free_owned(&parsed_headers);
    return SW_PARSE_UNSUPPORTED_CHUNKED;

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
        sw_char_array_append_bytes(&connection->write_buffer, chunk, read_bytes);
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

            if (sw_connection_create(mgr, listener, client_fd, host, (u16)atoi(service)) == NULL) {
                sw_socket_close(client_fd);
                return -1;
            }
        }
    }
}

int sw_mgr_connection_readable(sw_mgr* mgr, sw_connection* connection) {
    c8 chunk[4096];
    b8 read_any = 0;
    const sz max_request_bytes = sw_http_config_max_request_bytes(&mgr->config);

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
            sw_char_array_append_bytes(&connection->read_buffer, chunk, (sz)read_bytes);
            sw_connection_mark_activity(connection, sw_get_time());
            if (sw_char_array_size(&connection->read_buffer) > max_request_bytes) {
                sw_http_reply_status_text(connection, 413, "Payload Too Large");
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

    switch (sw_connection_try_parse_request(connection)) {
        case SW_PARSE_READY:
            if (connection->handler != NULL) {
                connection->handler(connection, &connection->request, connection->handler_user_data);
                sw_mgr_sync_connection(mgr, connection);
            }
            return 0;
        case SW_PARSE_PENDING:
            return 0;
        case SW_PARSE_UNSUPPORTED_CHUNKED:
            sw_http_replyf(connection, 501, "text/plain; charset=utf-8", "Chunked requests are not supported");
            sw_mgr_sync_connection(mgr, connection);
            return 0;
        case SW_PARSE_HEADERS_TOO_LARGE:
        case SW_PARSE_TOO_MANY_HEADERS:
            sw_http_reply_status_text(connection, 431, "Request Header Fields Too Large");
            return 0;
        case SW_PARSE_PAYLOAD_TOO_LARGE:
            sw_http_reply_status_text(connection, 413, "Payload Too Large");
            return 0;
        case SW_PARSE_BAD_REQUEST:
        default:
            sw_http_replyf(connection, 400, "text/plain; charset=utf-8", "Bad Request");
            sw_mgr_sync_connection(mgr, connection);
            return 0;
    }
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

    sw_connection_fill_file_buffer(connection);

    while (sw_char_array_size(&connection->write_buffer) > 0) {
        const c8* data = sw_char_array_data(&connection->write_buffer);
        const sz data_len = sw_char_array_size(&connection->write_buffer);
        int sent_bytes = 0;
#if defined(SYPHAX_WEB_HAS_TLS)
        const int send_rc = sw_connection_transport_send(connection, data, data_len, &sent_bytes);
        if (send_rc > 0) {
#else
        sent_bytes = (int)send(connection->fd, data, (int)data_len, 0);
        if (sent_bytes > 0) {
#endif
            sw_connection_mark_activity(connection, sw_get_time());
            sw_char_array_consume_prefix(&connection->write_buffer, (sz)sent_bytes);
            if (sw_char_array_size(&connection->write_buffer) == 0) {
                sw_connection_fill_file_buffer(connection);
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
        sw_connection_fill_file_buffer(connection);
    }

    if (sw_char_array_size(&connection->write_buffer) == 0 && connection->file_stream == NULL && connection->close_after_write) {
        return sw_connection_shutdown_send(mgr, connection);
    }

    sw_mgr_sync_connection(mgr, connection);
    return 0;
}

sw_http_config sw_http_config_default(void) {
    const sw_http_config config = {
        .max_header_bytes = 16 * 1024,
        .max_body_bytes = 1024 * 1024,
        .max_header_count = 64,
        .idle_timeout_ms = 15 * 1000,
        .header_timeout_ms = 5 * 1000,
        .body_timeout_ms = 15 * 1000
    };
    return config;
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
    sw_mgr* mgr;
    const sw_http_config defaults = sw_http_config_default();

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
    mgr->config = defaults;
    if (config != NULL) {
        if (config->max_header_bytes > 0) {
            mgr->config.max_header_bytes = config->max_header_bytes;
        }
        if (config->max_body_bytes > 0) {
            mgr->config.max_body_bytes = config->max_body_bytes;
        }
        if (config->max_header_count > 0) {
            mgr->config.max_header_count = config->max_header_count;
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
    }

    if (sw_backend_init(mgr) != 0) {
        s_array_clear(&mgr->listeners);
        s_array_clear(&mgr->connections);
        free(mgr);
        sw_socket_runtime_release();
        return NULL;
    }

    return mgr;
}

void sw_mgr_destroy(sw_mgr* mgr) {
    if (mgr == NULL) {
        return;
    }

    while (s_array_get_size(&mgr->connections) > 0) {
        sw_connection* connection = s_array_get_data(&mgr->connections)[0];
        sw_mgr_close_connection(mgr, connection);
    }

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

    listener_fd = sw_create_listener_socket(host, port, &bound_port);
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

i32 sw_https_listen(sw_mgr* mgr, const c8* url, const sw_tls_config* tls, sw_http_handler handler, void* user_data) {
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

    listener_fd = sw_create_listener_socket(host, port, &bound_port);
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

static i32 sw_connection_remaining_timeout_ms(const sw_mgr* mgr, const sw_connection* connection, f64 now_ms) {
    f64 remaining_ms = -1.0;

    if (mgr == NULL || connection == NULL) {
        return -1;
    }

    if (connection->write_shutdown) {
        if (mgr->config.idle_timeout_ms > 0) {
            const f64 idle_remaining_ms = (f64)mgr->config.idle_timeout_ms - (now_ms - connection->last_activity_at_ms);
            if (idle_remaining_ms <= 0.0) {
                return 0;
            }
            if (idle_remaining_ms > (f64)INT_MAX) {
                return INT_MAX;
            }
            return (i32)idle_remaining_ms;
        }
        return -1;
    }

    if (connection->close_after_write && sw_connection_has_pending_output(connection)) {
        return -1;
    }

#if defined(SYPHAX_WEB_HAS_TLS)
    if (sw_connection_tls_handshake_pending(connection) && connection->tls_handshake_timeout_ms > 0) {
        remaining_ms = (f64)connection->tls_handshake_timeout_ms - (now_ms - connection->tls_handshake_started_at_ms);
    }
#endif

    if (!connection->request_ready
#if defined(SYPHAX_WEB_HAS_TLS)
        && !sw_connection_tls_handshake_pending(connection)
#endif
    ) {
        const i32 phase_timeout_ms = connection->headers_complete ? mgr->config.body_timeout_ms : mgr->config.header_timeout_ms;
        if (phase_timeout_ms > 0) {
            const f64 phase_remaining_ms = (f64)phase_timeout_ms - (now_ms - connection->phase_started_at_ms);
            if (remaining_ms < 0.0 || phase_remaining_ms < remaining_ms) {
                remaining_ms = phase_remaining_ms;
            }
        }
    }

    if (mgr->config.idle_timeout_ms > 0) {
        const f64 idle_remaining_ms = (f64)mgr->config.idle_timeout_ms - (now_ms - connection->last_activity_at_ms);
        if (remaining_ms < 0.0 || idle_remaining_ms < remaining_ms) {
            remaining_ms = idle_remaining_ms;
        }
    }

    if (remaining_ms < 0.0) {
        return -1;
    }
    if (remaining_ms <= 0.0) {
        return 0;
    }
    if (remaining_ms > (f64)INT_MAX) {
        return INT_MAX;
    }
    return (i32)remaining_ms;
}

static i32 sw_mgr_effective_poll_timeout(const sw_mgr* mgr, i32 timeout_ms) {
    i32 effective_timeout_ms = timeout_ms;
    const f64 now_ms = sw_get_time();
    sw_connection* const* connections;
    sz i;

    if (mgr == NULL) {
        return timeout_ms;
    }
    connections = (sw_connection* const*)s_array_get_data((sw_connection_array*)&mgr->connections);

    for (i = 0; i < s_array_get_size(&mgr->connections); ++i) {
        sw_connection* connection = connections[i];
        const i32 remaining_ms = sw_connection_remaining_timeout_ms(mgr, connection, now_ms);
        if (remaining_ms < 0) {
            continue;
        }
        if (effective_timeout_ms < 0 || remaining_ms < effective_timeout_ms) {
            effective_timeout_ms = remaining_ms;
        }
    }

    return effective_timeout_ms;
}

static void sw_mgr_enforce_timeouts(sw_mgr* mgr) {
    const f64 now_ms = sw_get_time();
    sz i = 0;

    if (mgr == NULL) {
        return;
    }

    while (i < s_array_get_size(&mgr->connections)) {
        sw_connection* connection = s_array_get_data(&mgr->connections)[i];
        const i32 remaining_ms = sw_connection_remaining_timeout_ms(mgr, connection, now_ms);

        if (remaining_ms == 0) {
            if (connection->write_shutdown) {
                sw_mgr_close_connection(mgr, connection);
                continue;
            }
#if defined(SYPHAX_WEB_HAS_TLS)
            if (sw_connection_tls_handshake_pending(connection)) {
                sw_mgr_close_connection(mgr, connection);
                continue;
            }
#endif
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
        ++i;
    }
}

i32 sw_mgr_poll(sw_mgr* mgr, i32 timeout_ms) {
    int rc;

    if (mgr == NULL || mgr->stop_requested) {
        return 0;
    }
    sw_mgr_enforce_timeouts(mgr);
    timeout_ms = sw_mgr_effective_poll_timeout(mgr, timeout_ms);
    rc = sw_backend_poll(mgr, timeout_ms);
    if (rc < 0) {
        return -1;
    }
    sw_mgr_enforce_timeouts(mgr);
    return rc;
}

void sw_mgr_request_stop(sw_mgr* mgr) {
    if (mgr != NULL) {
        mgr->stop_requested = 1;
    }
}

b8 sw_mgr_is_running(const sw_mgr* mgr) {
    return mgr != NULL && !mgr->stop_requested;
}

u16 sw_mgr_get_listener_port(const sw_mgr* mgr, sz listener_index) {
    sw_listener* const* listeners;
    if (mgr == NULL || listener_index >= s_array_get_size(&mgr->listeners)) {
        return 0;
    }
    listeners = (sw_listener* const*)s_array_get_data((sw_listener_array*)&mgr->listeners);
    return listeners[listener_index]->bound_port;
}

i32 sw_server_listen(const c8* url, const sw_http_config* config, sw_http_handler handler, void* user_data) {
    sw_mgr* mgr = sw_mgr_create(config);
    int rc;

    if (mgr == NULL) {
        return -1;
    }

    rc = sw_http_listen(mgr, url, handler, user_data);
    if (rc != 0) {
        sw_mgr_destroy(mgr);
        return rc;
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

    sw_mgr_destroy(mgr);
    return rc;
}

i32 sw_server_listen_tls(
    const c8* url,
    const sw_http_config* config,
    const sw_tls_config* tls,
    sw_http_handler handler,
    void* user_data
) {
    sw_mgr* mgr = sw_mgr_create(config);
    int rc;

    if (mgr == NULL) {
        return -1;
    }

    rc = sw_https_listen(mgr, url, tls, handler, user_data);
    if (rc != 0) {
        sw_mgr_destroy(mgr);
        return rc;
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

    sw_mgr_destroy(mgr);
    return rc;
}

static int sw_connection_begin_response(sw_connection* connection, i32 status_code, const c8* content_type, sz content_length) {
    char headers[512];
    int written = snprintf(headers, sizeof(headers),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %zu\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code,
        sw_http_status_text(status_code),
        (content_type != NULL) ? content_type : "text/plain; charset=utf-8",
        content_length);

    if (written < 0) {
        return -1;
    }

    sw_char_array_reset(&connection->write_buffer);
    connection->close_after_write = 1;
    return sw_char_array_append_bytes(&connection->write_buffer, headers, (sz)written) ? 0 : -1;
}

i32 sw_http_reply(sw_connection* connection, i32 status_code, const c8* content_type, const void* body, sz body_len) {
    if (connection == NULL) {
        return -1;
    }
    if (sw_connection_begin_response(connection, status_code, content_type, body_len) != 0) {
        return -1;
    }
    if (body_len > 0 && !sw_char_array_append_bytes(&connection->write_buffer, body, body_len)) {
        return -1;
    }
    sw_connection_mark_activity(connection, sw_get_time());
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
    sw_char_array_append_vformat(&body, fmt, ap);
    va_end(ap);

    rc = sw_http_reply(connection, status_code, content_type, sw_char_array_data(&body), sw_char_array_size(&body));
    sw_char_array_free(&body);
    return rc;
}

i32 sw_http_write(sw_connection* connection, const void* data, sz data_len) {
    if (connection == NULL) {
        return -1;
    }
    if (!sw_char_array_append_bytes(&connection->write_buffer, data, data_len)) {
        return -1;
    }
    sw_connection_mark_activity(connection, sw_get_time());
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

    if (!ok) {
        return -1;
    }
    sw_connection_mark_activity(connection, sw_get_time());
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

i32 sw_http_serve_file(sw_connection* connection, const c8* path) {
    struct stat st;
    FILE* file;

    if (connection == NULL || path == NULL) {
        return -1;
    }

    if (strstr(path, "..") != NULL) {
        return sw_http_replyf(connection, 403, "text/plain; charset=utf-8", "Forbidden");
    }

    if (stat(path, &st) != 0) {
        return sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "File not found");
    }

    if (S_ISDIR(st.st_mode)) {
        return sw_http_replyf(connection, 403, "text/plain; charset=utf-8", "Directory listing not allowed");
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
    sw_connection_fill_file_buffer(connection);
    sw_connection_mark_activity(connection, sw_get_time());
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
    return sw_http_serve_file(connection, target_real);
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

i32 sw_http_get_form(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len) {
    if (buf != NULL && buf_len > 0) {
        buf[0] = '\0';
    }
    if (hm == NULL || name == NULL || buf == NULL || buf_len == 0) {
        return -1;
    }
    if (hm->body == NULL || hm->body_len == 0) {
        return 0;
    }

    return sw_http_decode_var(hm->body, hm->body_len, name, buf, buf_len, 0);
}

i32 sw_http_get_var(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len) {
    if (hm == NULL || hm->body == NULL) {
        if (buf != NULL && buf_len > 0) {
            buf[0] = '\0';
        }
        return -1;
    }

    return sw_http_decode_var(hm->body, hm->body_len, name, buf, buf_len, -1);
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

    if (hm == NULL || mp == NULL || offset == NULL || hm->body == NULL || *offset > body_len) {
        return 0;
    }

    content_type = sw_http_header_get(hm, "Content-Type");
    if (content_type == NULL || !sw_http_content_type_boundary(content_type, boundary, sizeof(boundary))) {
        return 0;
    }
    if (snprintf(boundary_marker, sizeof(boundary_marker), "--%s", boundary) < 0) {
        return 0;
    }
    if (snprintf(next_boundary_marker, sizeof(next_boundary_marker), "\r\n%s", boundary_marker) < 0) {
        return 0;
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
            mp->filename = sw_parse_disposition_param(line, line_len, "filename");
        } else if (sw_header_value_matches_name(line, line_len, "Content-Type")) {
            const c8* colon = memchr(line, ':', line_len);
            if (colon != NULL) {
                mp->content_type = sw_strdup_trimmed_range(colon + 1, line_len - (sz)(colon + 1 - line));
            }
        }

        line_offset = line_end + 2;
    }

    *offset = (sz)(next_boundary - hm->body);
    return 1;
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
