#ifndef SW_INTERNAL_H
#define SW_INTERNAL_H

#ifndef _WIN32
#    ifndef _POSIX_C_SOURCE
#        define _POSIX_C_SOURCE 200809L
#    endif
#endif

#include "syphax/s_array.h"
#include "sw_server.h"
#include "sw_translator.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
typedef SOCKET sw_socket;
#    define SW_INVALID_SOCKET INVALID_SOCKET
#else
#    include <arpa/inet.h>
#    include <fcntl.h>
#    include <netdb.h>
#    include <sys/socket.h>
#    include <sys/types.h>
#    include <unistd.h>
typedef int sw_socket;
#    define SW_INVALID_SOCKET (-1)
#endif

typedef struct sw_backend sw_backend;
typedef struct sw_listener sw_listener;
typedef struct sw_connection sw_connection;

typedef s_array(c8, sw_char_array);
typedef s_array(b8, sw_bool_array);
typedef s_array(sw_http_header, sw_http_header_array);
typedef s_array(sw_listener*, sw_listener_array);
typedef s_array(sw_connection*, sw_connection_array);
typedef s_array(sw_language, sw_languages);

typedef struct {
    c8* key;
    c8* value;
} sw_translation_item;

typedef s_array(sw_translation_item, sw_translation_item_array);

typedef struct {
    c8* code;
    sw_translation_item_array items;
} sw_translation_language;

typedef s_array(sw_translation_language, sw_translation_language_array);

struct sw_translator {
    sw_languages languages;
    sw_translation_language_array translations;
    s_handle current_language;
};

typedef enum {
    SW_SOURCE_LISTENER = 1,
    SW_SOURCE_CONNECTION = 2
} sw_source_kind;

struct sw_buffer {
    sw_char_array bytes;
    sw_bool_array translation_stack;
    const sw_translator* translator;
    b8 translation_enabled;
    b8 html_doctype_emitted;
    b8 js_runtime_emitted;
};

struct sw_listener {
    sw_source_kind source_kind;
    sw_socket fd;
    sw_http_handler handler;
    void* handler_user_data;
    s_handle array_handle;
    struct sw_mgr* mgr;
    u16 bound_port;
};

struct sw_connection {
    sw_source_kind source_kind;
    sw_socket fd;
    struct sw_mgr* mgr;
    sw_listener* listener;
    void* user_data;
    sw_http_handler handler;
    void* handler_user_data;
    s_handle array_handle;
    sw_char_array read_buffer;
    sw_char_array write_buffer;
    sw_http_header_array header_storage;
    sw_http_message request;
    b8 request_ready;
    b8 close_after_write;
    FILE* file_stream;
    sz file_remaining;
    c8 remote_ip[64];
    u16 remote_port;
};

struct sw_mgr {
    sw_listener_array listeners;
    sw_connection_array connections;
    b8 stop_requested;
    sw_backend* backend;
};

char* sw_strdup_cstr(const c8* str);
char* sw_strdup_range(const c8* str, sz len);
int sw_stricmp_ascii(const c8* lhs, const c8* rhs);
const c8* sw_strcasestr_ascii(const c8* haystack, const c8* needle);

void sw_char_array_init(sw_char_array* array);
void sw_char_array_free(sw_char_array* array);
void sw_char_array_reset(sw_char_array* array);
b8 sw_char_array_append_byte(sw_char_array* array, c8 value);
b8 sw_char_array_append_bytes(sw_char_array* array, const void* data, sz len);
b8 sw_char_array_append_cstr(sw_char_array* array, const c8* str);
b8 sw_char_array_append_vformat(sw_char_array* array, const c8* fmt, va_list ap);
void sw_char_array_consume_prefix(sw_char_array* array, sz count);
const c8* sw_char_array_data(const sw_char_array* array);
sz sw_char_array_size(const sw_char_array* array);

int sw_socket_runtime_acquire(void);
void sw_socket_runtime_release(void);
int sw_socket_set_nonblocking(sw_socket fd);
int sw_socket_close(sw_socket fd);
int sw_socket_last_error(void);
b8 sw_socket_error_is_would_block(int err);

const c8* sw_http_status_text(i32 status_code);

int sw_mgr_accept_ready(sw_mgr* mgr, sw_listener* listener);
int sw_mgr_connection_readable(sw_mgr* mgr, sw_connection* connection);
int sw_mgr_connection_writable(sw_mgr* mgr, sw_connection* connection);
int sw_mgr_sync_connection(sw_mgr* mgr, sw_connection* connection);
void sw_mgr_close_connection(sw_mgr* mgr, sw_connection* connection);
void sw_connection_reset_request(sw_connection* connection);
b8 sw_connection_has_pending_output(const sw_connection* connection);

#endif
