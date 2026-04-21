#include "syphax/web/sw_html.h"
#include "syphax/web/sw_server.h"
#include "syphax/web/sw_translator.h"
#include "syphax/web/sw_utility.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    include <winsock2.h>
#    include <ws2tcpip.h>
#    include <windows.h>
#else
#    include <arpa/inet.h>
#    include <fcntl.h>
#    include <netinet/in.h>
#    include <sys/socket.h>
#    include <unistd.h>
#endif

typedef struct {
    const c8* file_path;
    int request_count;
} sw_test_server_state;

typedef struct {
    char* data;
    sz size;
    b8 closed;
} sw_test_response;

#ifdef _WIN32
typedef SOCKET sw_test_socket;
#else
typedef int sw_test_socket;
#endif

static void sw_test_handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    sw_test_server_state* state = (sw_test_server_state*)user_data;
    char name[128];

    state->request_count += 1;

    if (strcmp(request->method, "GET") == 0 && strcmp(request->uri, "/") == 0) {
        sw_html_buffer* html = sw_html_buffer_create();
        sw_html_raw(html, "<!doctype html>");
        sw_html_open_tag(html, "html", NULL);
        sw_html_open_tag(html, "body", NULL);
        sw_html_open_tag(html, "h1", NULL);
        sw_html_text(html, "Syphax Web");
        sw_html_close_tag(html, "h1");
        sw_html_close_tag(html, "body");
        sw_html_close_tag(html, "html");
        sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_html_buffer_data(html), sw_html_buffer_size(html));
        sw_html_buffer_destroy(html);
        return;
    }

    if (strcmp(request->method, "POST") == 0 && strcmp(request->uri, "/form") == 0) {
        assert(sw_http_get_var(request, "name", name, sizeof(name)) > 0);
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "name=%s", name);
        return;
    }

    if (strcmp(request->method, "GET") == 0 && strcmp(request->uri, "/file") == 0) {
        assert(sw_http_serve_file(connection, state->file_path) == 0);
        return;
    }

    if (strcmp(request->method, "GET") == 0 && strcmp(request->uri, "/style.css") == 0) {
        const c8 css[] = "body { color: #fff; }\n";
        assert(sw_http_reply(connection, 200, "text/css; charset=utf-8", css, sizeof(css) - 1) == 0);
        return;
    }

    sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "Not Found");
}

static void test_translator(void) {
    sw_translator* translator = sw_translator_create();
    assert(translator != NULL);
    assert(sw_translator_set_language(translator, "fr"));
    assert(strcmp(sw_translator_get_language(translator), "fr") == 0);
    assert(strcmp(sw_translate(translator, "Search"), "Rechercher") == 0);
    assert(strcmp(sw_translate(translator, "Unknown"), "Unknown") == 0);
    sw_translator_destroy(translator);
}

static void test_html_builder(void) {
    sw_translator* translator = sw_translator_create();
    sw_html_buffer* buffer = sw_html_buffer_create();

    assert(sw_translator_set_language(translator, "fr"));
    sw_html_buffer_set_translator(buffer, translator);

    assert(sw_html_open_tag(buffer, "div", &sw_html_attr(.class_name = "shell", .title = "Search")));
    assert(sw_html_void_tag(buffer, "input", &sw_html_attr(.type = "text", .placeholder = "Search", .value = "\"quoted\"")));
    assert(sw_html_text(buffer, "<safe & sound>"));
    assert(sw_html_close_tag(buffer, "div"));

    assert(strstr(sw_html_buffer_data(buffer), "title=\"Rechercher\"") != NULL);
    assert(strstr(sw_html_buffer_data(buffer), "placeholder=\"Rechercher\"") != NULL);
    assert(strstr(sw_html_buffer_data(buffer), "value=\"&quot;quoted&quot;\"") != NULL);
    assert(strstr(sw_html_buffer_data(buffer), "&lt;safe &amp; sound&gt;") != NULL);

    sw_html_buffer_destroy(buffer);
    sw_translator_destroy(translator);
}

static void test_request_helpers(void) {
    sw_http_header headers[] = {
        { "Content-Type", "multipart/form-data; boundary=demo" }
    };
    const c8 body[] =
        "--demo\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"hello.txt\"\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "payload\r\n"
        "--demo--\r\n";
    const sw_http_message message = {
        .method = "POST",
        .uri = "/upload",
        .proto = "HTTP/1.1",
        .headers = headers,
        .num_headers = 1,
        .body = "name=Jane+Doe&city=Tokyo",
        .body_len = strlen("name=Jane+Doe&city=Tokyo"),
        .content_length = strlen("name=Jane+Doe&city=Tokyo")
    };
    sw_http_message multipart_message = {
        .method = "POST",
        .uri = "/upload",
        .proto = "HTTP/1.1",
        .headers = headers,
        .num_headers = 1,
        .body = body,
        .body_len = sizeof(body) - 1,
        .content_length = sizeof(body) - 1
    };
    char value[64];
    sz offset = 0;
    sw_http_multipart part;

    assert(sw_http_get_var(&message, "name", value, sizeof(value)) > 0);
    assert(strcmp(value, "Jane Doe") == 0);

    memset(&part, 0, sizeof(part));
    assert(sw_http_next_multipart(&multipart_message, &part, &offset) == 1);
    assert(strcmp(part.name, "file") == 0);
    assert(strcmp(part.filename, "hello.txt") == 0);
    assert(strcmp(part.content_type, "text/plain") == 0);
    assert(strncmp(part.data, "payload", part.data_len) == 0);
    sw_http_multipart_clear(&part);
}

static sw_test_socket connect_to_port(u16 port) {
#ifdef _WIN32
    sw_test_socket fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#else
    sw_test_socket fd = socket(AF_INET, SOCK_STREAM, 0);
#endif
    struct sockaddr_in address;
#ifndef _WIN32
    int flags;
#endif

#ifdef _WIN32
    assert(fd != INVALID_SOCKET);
#else
    assert(fd >= 0);
#endif

    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);

    assert(connect(fd, (struct sockaddr*)&address, sizeof(address)) == 0);

#ifdef _WIN32
    {
        u_long mode = 1;
        assert(ioctlsocket(fd, FIONBIO, &mode) == 0);
    }
#else
    flags = fcntl(fd, F_GETFL, 0);
    assert(flags >= 0);
    assert(fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0);
#endif
    return fd;
}

static sw_test_response issue_request(sw_mgr* mgr, u16 port, const c8* request) {
    sw_test_socket fd = connect_to_port(port);
    sw_test_response response = {0};
    sz response_len = 0;
    int attempts;

    response.data = (char*)calloc(1, 65536);
    assert(response.data != NULL);
    assert(send(fd, request, (int)strlen(request), 0) == (int)strlen(request));

    for (attempts = 0; attempts < 200; ++attempts) {
        char chunk[4096];
        int received;
        assert(sw_mgr_poll(mgr, 10) >= 0);
        received = (int)recv(fd, chunk, sizeof(chunk), 0);
        if (received > 0) {
            memcpy(response.data + response_len, chunk, (sz)received);
            response_len += (sz)received;
            response.data[response_len] = '\0';
            continue;
        }
        if (received == 0) {
            response.closed = 1;
            break;
        }
#ifdef _WIN32
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
#endif
            break;
        }
    }

#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    response.size = response_len;
    return response;
}

static sz response_content_length(const sw_test_response* response) {
    const char* header = strstr(response->data, "Content-Length:");
    assert(header != NULL);
    header += strlen("Content-Length:");
    while (*header == ' ') {
        ++header;
    }
    return (sz)strtoull(header, NULL, 10);
}

static sz response_body_size(const sw_test_response* response) {
    const char* body = strstr(response->data, "\r\n\r\n");
    assert(body != NULL);
    body += 4;
    return response->size - (sz)(body - response->data);
}

static void assert_complete_response(const sw_test_response* response) {
    assert(response->closed);
    assert(response_content_length(response) == response_body_size(response));
}

static void test_live_server(void) {
    sw_mgr* mgr = sw_mgr_create();
    sw_test_server_state state;
    char file_name[256];
    char file_path[512];
    FILE* file;
    u16 port;
    sw_test_response response;
    int listen_rc;

    assert(mgr != NULL);

    assert(sw_generate_unique_filename("fixture.txt", file_name, sizeof(file_name)));
    snprintf(file_path, sizeof(file_path), "%s", file_name);
    file = fopen(file_path, "wb");
    assert(file != NULL);
    fputs("static payload", file);
    fclose(file);

    state.file_path = file_path;
    state.request_count = 0;

    listen_rc = sw_http_listen(mgr, "http://127.0.0.1:0", sw_test_handler, &state);
    assert(listen_rc == 0);
    port = sw_mgr_get_listener_port(mgr, 0);
    assert(port != 0);

    response = issue_request(mgr, port, "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response.data, "<h1>Syphax Web</h1>") != NULL);
    free(response.data);

    response = issue_request(mgr, port,
        "POST /form HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Content-Length: 13\r\n"
        "\r\n"
        "name=Jane+Doe");
    assert_complete_response(&response);
    assert(strstr(response.data, "name=Jane Doe") != NULL);
    free(response.data);

    response = issue_request(mgr, port, "GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "Content-Type: text/css; charset=utf-8") != NULL);
    assert(strstr(response.data, "body { color: #fff; }") != NULL);
    free(response.data);

    response = issue_request(mgr, port, "GET /file HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "static payload") != NULL);
    free(response.data);

    assert(state.request_count == 4);
    remove(file_path);
    sw_mgr_destroy(mgr);
}

typedef void (*sw_test_fn)(void);

typedef struct {
    const char* name;
    sw_test_fn fn;
} sw_named_test;

static const sw_named_test sw_named_tests[] = {
    { "translator", test_translator },
    { "html_builder", test_html_builder },
    { "request_helpers", test_request_helpers },
    { "live_server", test_live_server }
};

static int run_named_test(const char* name) {
    sz i;

    for (i = 0; i < sizeof(sw_named_tests) / sizeof(sw_named_tests[0]); ++i) {
        if (strcmp(name, sw_named_tests[i].name) == 0) {
            sw_named_tests[i].fn();
            return 0;
        }
    }

    fprintf(stderr, "Unknown test '%s'\n", name);
    fprintf(stderr, "Available tests:");
    for (i = 0; i < sizeof(sw_named_tests) / sizeof(sw_named_tests[0]); ++i) {
        fprintf(stderr, " %s", sw_named_tests[i].name);
    }
    fputc('\n', stderr);
    return 1;
}

int main(int argc, char** argv) {
    sz i;

    if (argc > 1) {
        return run_named_test(argv[1]);
    }

    for (i = 0; i < sizeof(sw_named_tests) / sizeof(sw_named_tests[0]); ++i) {
        sw_named_tests[i].fn();
    }

    return 0;
}
