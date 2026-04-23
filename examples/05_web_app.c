#include "example_common.h"

static void handle_request(sw_connection* connection, const sw_http_message* request, void* user_data) {
    c8 root[1024];
    const c8* alpn;

    (void)user_data;

    if (serve_style(connection, request)) {
        return;
    }
    if (sw_http_is(request, "GET", "/api/summary")) {
        alpn = sw_connection_alpn(connection);
        (void)sw_http_replyf(connection, 200, "application/json; charset=utf-8",
            "{\"secure\":%s,\"alpn\":\"%s\",\"transport\":\"https\",\"app\":\"folder\"}\n",
            sw_connection_is_secure(connection) ? "true" : "false",
            (alpn != NULL && alpn[0] != '\0') ? alpn : "none");
        return;
    }

    if (request->method == NULL || strcmp(request->method, "GET") != 0) {
        (void)sw_http_replyf(connection, 405, "text/plain; charset=utf-8", "Method Not Allowed");
        return;
    }

    advanced_root(root, sizeof(root));
    if (sw_http_is(request, "GET", "/")) {
        (void)sw_http_serve_path(connection, root, "/index.html");
        return;
    }

    (void)sw_http_serve_path(connection, root, request->uri);
}

int main(void) {
    sw_http_config config = http_config();

    return listen_https(
        "https://127.0.0.1:8446",
        &config,
        handle_request,
        NULL,
        "Syphax-Web folder web app example"
    );
}
