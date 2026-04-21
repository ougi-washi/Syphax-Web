#include "syphax/web/sw_html.h"
#include "syphax/web/sw_server.h"
#include "syphax/web/sw_translator.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    sw_translator* translator;
} sw_example_state;

static void render_root(sw_connection* connection, sw_example_state* state) {
    sw_html_buffer* html = sw_html_buffer_create();

    sw_html_buffer_set_translator(html, state->translator);

    sw_html_raw(html, "<!doctype html>");
    sw_html_open_tag(html, "html", &sw_html_attr(.lang = sw_translator_get_language(state->translator)));
    sw_html_open_tag(html, "head", NULL);
    sw_html_meta_charset(html, "utf-8");
    sw_html_title(html, "Syphax Web");
    sw_html_void_tag(html, "link", &sw_html_attr(.rel = "stylesheet", .href = "/style.css"));
    sw_html_close_tag(html, "head");

    sw_html_open_tag(html, "body", &sw_html_attr(.class_name = "sw-body"));
    sw_html_open_tag(html, "main", &sw_html_attr(.class_name = "sw-shell"));
    sw_html_open_tag(html, "h1", NULL);
    sw_html_text(html, "Syphax Web");
    sw_html_close_tag(html, "h1");

    sw_html_open_tag(html, "p", NULL);
    sw_html_text(html, "Search");
    sw_html_raw(html, " helper with safe HTML escaping and a reusable server library.");
    sw_html_close_tag(html, "p");

    sw_html_void_tag(html, "input", &sw_html_attr(.type = "text", .placeholder = "Search", .value = "Search"));
    sw_html_open_tag(html, "p", NULL);
    sw_html_raw(html, "Static assets are served through the same library API.");
    sw_html_close_tag(html, "p");
    sw_html_close_tag(html, "main");
    sw_html_close_tag(html, "body");
    sw_html_close_tag(html, "html");

    sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_html_buffer_data(html), sw_html_buffer_size(html));
    sw_html_buffer_destroy(html);
}

static void http_handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    sw_example_state* state = (sw_example_state*)user_data;

    if (strcmp(request->method, "GET") == 0) {
        if (strcmp(request->uri, "/") == 0) {
            render_root(connection, state);
            return;
        }
        if (strcmp(request->uri, "/style.css") == 0) {
            sw_http_serve_file(connection, "resources/style.css");
            return;
        }
    }

    sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "Not Found");
}

int main(void) {
    sw_example_state state;

    state.translator = sw_translator_create();
    sw_translator_set_language(state.translator, "en");

    printf("Listening on http://0.0.0.0:8000\n");
    sw_server_listen("http://0.0.0.0:8000", http_handler, &state);

    sw_translator_destroy(state.translator);
    return 0;
}
