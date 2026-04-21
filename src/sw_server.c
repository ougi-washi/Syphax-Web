#include "sw_backend.h"
#include "sw_internal.h"

#include <sys/stat.h>

typedef enum {
    SW_PARSE_PENDING = 0,
    SW_PARSE_READY = 1,
    SW_PARSE_BAD_REQUEST = -1,
    SW_PARSE_UNSUPPORTED_CHUNKED = -2
} sw_parse_result;

static sz sw_socket_runtime_users = 0;
static sw_mgr* sw_signal_mgr = NULL;

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
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        default: return "Unknown";
    }
}

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

static b8 sw_parse_listen_url(const c8* url, c8* host, sz host_cap, c8* port, sz port_cap) {
    const c8* cursor = url;
    const c8* authority_end;
    const c8* host_begin;
    const c8* host_end;
    const c8* port_begin = NULL;
    sz host_len;
    sz port_len;

    if (url == NULL || host == NULL || port == NULL || host_cap == 0 || port_cap == 0) {
        return 0;
    }

    if (strncmp(cursor, "http://", 7) == 0) {
        cursor += 7;
    } else if (strncmp(cursor, "https://", 8) == 0) {
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
        snprintf(port, port_cap, "%d", 80);
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
    if (remote_ip != NULL) {
        snprintf(connection->remote_ip, sizeof(connection->remote_ip), "%s", remote_ip);
    }

    sw_char_array_init(&connection->read_buffer);
    sw_char_array_init(&connection->write_buffer);
    s_array_init(&connection->header_storage);

    handle = s_array_add(&mgr->connections, connection);
    connection->array_handle = handle;

    if (sw_backend_register_connection(mgr, connection) != 0) {
        s_array_remove_ordered(&mgr->connections, handle);
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
}

void sw_mgr_close_connection(sw_mgr* mgr, sw_connection* connection) {
    if (mgr == NULL || connection == NULL) {
        return;
    }

    sw_backend_unregister_connection(mgr, connection);

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

static sw_parse_result sw_connection_try_parse_request(sw_connection* connection) {
    const c8* data = sw_char_array_data(&connection->read_buffer);
    const sz data_len = sw_char_array_size(&connection->read_buffer);
    const sz header_end = sw_find_bytes(data, data_len, "\r\n\r\n", 4);
    const c8* first_line_end;
    const c8* cursor;
    sw_http_header_array parsed_headers;
    sw_http_message parsed_request;

    if (connection->request_ready) {
        return SW_PARSE_READY;
    }

    if (header_end == SIZE_MAX) {
        return SW_PARSE_PENDING;
    }

    first_line_end = strstr(data, "\r\n");
    if (first_line_end == NULL) {
        return SW_PARSE_PENDING;
    }

    memset(&parsed_request, 0, sizeof(parsed_request));
    s_array_init(&parsed_headers);

    {
        const c8* first_space = memchr(data, ' ', (sz)(first_line_end - data));
        const c8* second_space = NULL;

        if (first_space != NULL) {
            second_space = memchr(first_space + 1, ' ', (sz)(first_line_end - (first_space + 1)));
        }

        if (first_space == NULL || second_space == NULL) {
            s_array_clear(&parsed_headers);
            return SW_PARSE_BAD_REQUEST;
        }

        parsed_request.method = sw_strdup_range(data, (sz)(first_space - data));
        parsed_request.uri = sw_strdup_range(first_space + 1, (sz)(second_space - first_space - 1));
        parsed_request.proto = sw_strdup_range(second_space + 1, (sz)(first_line_end - second_space - 1));
    }

    cursor = first_line_end + 2;
    while ((sz)(cursor - data) < header_end) {
        const c8* line_end = strstr(cursor, "\r\n");
        const c8* colon;
        const c8* name_end;
        const c8* value_begin;
        const c8* value_end;
        sw_http_header header;
        char* name;
        char* value;

        if (line_end == NULL || line_end > data + header_end) {
            break;
        }
        if (cursor == line_end) {
            break;
        }

        colon = memchr(cursor, ':', (sz)(line_end - cursor));
        if (colon == NULL) {
            sw_connection_reset_request(connection);
            s_array_clear(&parsed_headers);
            free((void*)parsed_request.method);
            free((void*)parsed_request.uri);
            free((void*)parsed_request.proto);
            return SW_PARSE_BAD_REQUEST;
        }

        name_end = colon;
        while (name_end > cursor && isspace((unsigned char)name_end[-1])) {
            --name_end;
        }

        value_begin = colon + 1;
        while (value_begin < line_end && isspace((unsigned char)*value_begin)) {
            ++value_begin;
        }
        value_end = line_end;
        while (value_end > value_begin && isspace((unsigned char)value_end[-1])) {
            --value_end;
        }

        name = sw_strdup_range(cursor, (sz)(name_end - cursor));
        value = sw_strdup_range(value_begin, (sz)(value_end - value_begin));
        header.name = name;
        header.value = value;
        s_array_add(&parsed_headers, header);

        if (sw_stricmp_ascii(name, "Content-Length") == 0) {
            parsed_request.content_length = (sz)strtoull(value, NULL, 10);
        } else if (sw_stricmp_ascii(name, "Transfer-Encoding") == 0) {
            if (sw_strcasestr_ascii(value, "chunked") != NULL) {
                parsed_request.is_chunked = 1;
            }
        }

        cursor = line_end + 2;
    }

    if (parsed_request.is_chunked) {
        sz i;
        free((void*)parsed_request.method);
        free((void*)parsed_request.uri);
        free((void*)parsed_request.proto);
        for (i = 0; i < s_array_get_size(&parsed_headers); ++i) {
            free((void*)s_array_get_data(&parsed_headers)[i].name);
            free((void*)s_array_get_data(&parsed_headers)[i].value);
        }
        s_array_clear(&parsed_headers);
        return SW_PARSE_UNSUPPORTED_CHUNKED;
    }

    if (data_len < header_end + 4 + parsed_request.content_length) {
        sz i;
        free((void*)parsed_request.method);
        free((void*)parsed_request.uri);
        free((void*)parsed_request.proto);
        for (i = 0; i < s_array_get_size(&parsed_headers); ++i) {
            free((void*)s_array_get_data(&parsed_headers)[i].name);
            free((void*)s_array_get_data(&parsed_headers)[i].value);
        }
        s_array_clear(&parsed_headers);
        return SW_PARSE_PENDING;
    }

    parsed_request.body = sw_strdup_range(data + header_end + 4, parsed_request.content_length);
    parsed_request.body_len = parsed_request.content_length;
    parsed_request.headers = s_array_get_data(&parsed_headers);
    parsed_request.num_headers = s_array_get_size(&parsed_headers);

    connection->header_storage = parsed_headers;
    connection->request = parsed_request;
    connection->request_ready = 1;
    return SW_PARSE_READY;
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

    for (;;) {
        int read_bytes = (int)recv(connection->fd, chunk, sizeof(chunk), 0);
        if (read_bytes > 0) {
            read_any = 1;
            sw_char_array_append_bytes(&connection->read_buffer, chunk, (sz)read_bytes);
            continue;
        }
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
        case SW_PARSE_BAD_REQUEST:
        default:
            sw_http_replyf(connection, 400, "text/plain; charset=utf-8", "Bad Request");
            sw_mgr_sync_connection(mgr, connection);
            return 0;
    }
}

int sw_mgr_connection_writable(sw_mgr* mgr, sw_connection* connection) {
    sw_connection_fill_file_buffer(connection);

    while (sw_char_array_size(&connection->write_buffer) > 0) {
        const c8* data = sw_char_array_data(&connection->write_buffer);
        const sz data_len = sw_char_array_size(&connection->write_buffer);
        int sent_bytes = (int)send(connection->fd, data, (int)data_len, 0);
        if (sent_bytes > 0) {
            sw_char_array_consume_prefix(&connection->write_buffer, (sz)sent_bytes);
            if (sw_char_array_size(&connection->write_buffer) == 0) {
                sw_connection_fill_file_buffer(connection);
            }
            continue;
        }

        {
            const int err = sw_socket_last_error();
            if (sw_socket_error_is_would_block(err)) {
                break;
            }
        }

        sw_mgr_close_connection(mgr, connection);
        return -1;
    }

    if (sw_char_array_size(&connection->write_buffer) == 0) {
        sw_connection_fill_file_buffer(connection);
    }

    if (sw_char_array_size(&connection->write_buffer) == 0 && connection->file_stream == NULL && connection->close_after_write) {
        sw_mgr_close_connection(mgr, connection);
        return -1;
    }

    sw_mgr_sync_connection(mgr, connection);
    return 0;
}

sw_mgr* sw_mgr_create(void) {
    sw_mgr* mgr;

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

i32 sw_mgr_poll(sw_mgr* mgr, i32 timeout_ms) {
    if (mgr == NULL || mgr->stop_requested) {
        return 0;
    }
    return sw_backend_poll(mgr, timeout_ms);
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
    listeners = (sw_listener* const*)mgr->listeners.b.data;
    return listeners[listener_index]->bound_port;
}

i32 sw_server_listen(const c8* url, sw_http_handler handler, void* user_data) {
    sw_mgr* mgr = sw_mgr_create();
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
    return sw_mgr_sync_connection(connection->mgr, connection);
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
    return sw_mgr_sync_connection(connection->mgr, connection);
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

static int sw_hex_value(c8 ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

i32 sw_http_get_var(const sw_http_message* hm, const c8* name, c8* buf, sz buf_len) {
    const c8* body = (hm != NULL) ? hm->body : NULL;
    const sz name_len = (name != NULL) ? strlen(name) : 0;
    const c8* cursor;

    if (body == NULL || name == NULL || buf == NULL || buf_len == 0) {
        return -1;
    }

    cursor = body;
    while (*cursor != '\0') {
        const c8* value_begin;
        const c8* value_end;
        sz written = 0;

        if (strncmp(cursor, name, name_len) == 0 && cursor[name_len] == '=') {
            value_begin = cursor + name_len + 1;
            value_end = strchr(value_begin, '&');
            if (value_end == NULL) {
                value_end = value_begin + strlen(value_begin);
            }

            while (value_begin < value_end && written + 1 < buf_len) {
                if (*value_begin == '+' ) {
                    buf[written++] = ' ';
                    ++value_begin;
                } else if (*value_begin == '%' && value_begin + 2 < value_end) {
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

        cursor = strchr(cursor, '&');
        if (cursor == NULL) {
            break;
        }
        ++cursor;
    }

    return -1;
}

i32 sw_http_next_multipart(const sw_http_message* hm, sw_http_multipart* mp, sz* offset) {
    const c8* content_type;
    const c8* boundary;
    char boundary_str[256];
    const c8* part_start;
    const c8* headers_end;
    const c8* body_start;
    const c8* next_boundary;
    const c8* line;

    if (hm == NULL || mp == NULL || offset == NULL || hm->body == NULL) {
        return 0;
    }

    content_type = sw_http_header_get(hm, "Content-Type");
    if (content_type == NULL) {
        return 0;
    }

    boundary = strstr(content_type, "boundary=");
    if (boundary == NULL) {
        return 0;
    }
    boundary += 9;

    snprintf(boundary_str, sizeof(boundary_str), "--%s", boundary);
    part_start = strstr(hm->body + *offset, boundary_str);
    if (part_start == NULL) {
        return 0;
    }

    part_start += strlen(boundary_str);
    if (strncmp(part_start, "--", 2) == 0) {
        return 0;
    }
    if (strncmp(part_start, "\r\n", 2) == 0) {
        part_start += 2;
    }

    headers_end = strstr(part_start, "\r\n\r\n");
    if (headers_end == NULL) {
        return 0;
    }

    body_start = headers_end + 4;
    next_boundary = strstr(body_start, boundary_str);
    if (next_boundary == NULL) {
        return 0;
    }

    memset(mp, 0, sizeof(*mp));
    mp->boundary = sw_strdup_cstr(boundary);
    mp->data = body_start;
    mp->data_len = (sz)(next_boundary - body_start);
    if (mp->data_len >= 2 && body_start[mp->data_len - 2] == '\r' && body_start[mp->data_len - 1] == '\n') {
        mp->data_len -= 2;
    }

    line = part_start;
    while (line < headers_end) {
        const c8* line_end = strstr(line, "\r\n");
        if (line_end == NULL || line_end > headers_end) {
            break;
        }

        if (strncmp(line, "Content-Disposition:", 20) == 0) {
            const c8* name = strstr(line, "name=\"");
            const c8* filename = strstr(line, "filename=\"");
            if (name != NULL) {
                const c8* name_end;
                name += 6;
                name_end = strchr(name, '"');
                if (name_end != NULL) {
                    mp->name = sw_strdup_range(name, (sz)(name_end - name));
                }
            }
            if (filename != NULL) {
                const c8* filename_end;
                filename += 10;
                filename_end = strchr(filename, '"');
                if (filename_end != NULL) {
                    mp->filename = sw_strdup_range(filename, (sz)(filename_end - filename));
                }
            }
        } else if (strncmp(line, "Content-Type:", 13) == 0) {
            const c8* value = line + 13;
            while (*value != '\0' && isspace((unsigned char)*value)) {
                ++value;
            }
            mp->content_type = sw_strdup_range(value, (sz)(line_end - value));
        }

        line = line_end + 2;
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

void* sw_connection_user_data(sw_connection* connection) {
    return (connection != NULL) ? connection->user_data : NULL;
}

void sw_connection_set_user_data(sw_connection* connection, void* user_data) {
    if (connection != NULL) {
        connection->user_data = user_data;
    }
}
