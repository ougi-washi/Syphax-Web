#include "sw_html.h"
#include "sw_server.h"
#include "sw_translator.h"
#include "sw_utility.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    sw_translator* translator;
} sw_example_state;

typedef struct {
    const c8* title;
    const c8* body;
} sw_example_feature;

static const sw_example_feature sw_example_features[] = {
    { "Search", "Translated labels stay explicit and escaped." },
    { "Language", "Custom data-* attributes no longer require a fixed struct field." },
    { "Login", "Pages are easier to split into small C render functions." }
};

static const c8 sw_embedded_style[] =
    ":root{color-scheme:dark;"
    "--bg:#1b1c20;--panel:#23252b;--panel-2:#1f2126;--border:#343741;--border-strong:#444854;"
    "--text:#eceef2;--muted:#a0a4ad;--muted-2:#7f858f;--shadow:0 18px 48px rgba(0,0,0,.35);"
    "--radius:14px;--font-sans:system-ui,-apple-system,\"Segoe UI\",sans-serif;"
    "--font-mono:ui-monospace,SFMono-Regular,Menlo,Consolas,\"Liberation Mono\",monospace;}"
    "*{box-sizing:border-box;}html,body{min-height:100%;}"
    "body{margin:0;background:var(--bg);color:var(--text);font-family:var(--font-sans);line-height:1.6;}"
    ".sw-body{min-height:100vh;display:flex;align-items:flex-start;justify-content:center;padding:48px 20px;background:var(--bg);}"
    ".sw-shell{width:min(100%,760px);padding:28px;background:var(--panel);border:1px solid var(--border);border-radius:var(--radius);box-shadow:var(--shadow);}"
    ".sw-shell>*+*{margin-top:16px;}h1{margin:0 0 6px 0;font-size:1rem;font-weight:600;letter-spacing:.08em;text-transform:uppercase;font-family:var(--font-mono);}"
    "p{margin:0;color:var(--muted);max-width:56ch;}"
    "input[type=\"text\"]{width:100%;min-height:46px;padding:12px 14px;border:1px solid var(--border);border-radius:10px;background:var(--panel-2);color:var(--text);font:500 .95rem/1.4 var(--font-mono);outline:none;}"
    "input[type=\"text\"]::placeholder{color:var(--muted-2);}"
    "input[type=\"text\"]:focus{border-color:var(--border-strong);box-shadow:0 0 0 3px rgba(255,255,255,.04);}"
    ".sw-list{margin:0;padding-left:18px;color:var(--muted);}"
    ".sw-list li+li{margin-top:10px;}"
    ".sw-list strong{display:block;margin-bottom:2px;color:var(--text);font-family:var(--font-mono);font-size:.82rem;text-transform:uppercase;letter-spacing:.08em;}"
    "::selection{background:#3a3d46;color:var(--text);}"
    "@media (max-width:640px){.sw-body{padding:20px 12px;}.sw-shell{padding:18px;}}";

static void render_stylesheet(sw_connection* connection) {
    static const c8* const candidates[] = {
        "resources/style.css",
        "../resources/style.css"
    };
    sz i;

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); ++i) {
        sz size = 0;
        c8* css = sw_get_file_content(candidates[i], &size);
        if (css != NULL) {
            sw_http_reply(connection, 200, "text/css; charset=utf-8", css, size);
            free(css);
            return;
        }
    }

    sw_http_reply(connection, 200, "text/css; charset=utf-8", sw_embedded_style, strlen(sw_embedded_style));
}

static void render_feature(sw_html_buffer* html, const sw_example_feature* feature) {
    sw_html_open_tag(html, "li", NULL, 0);
    sw_html_open_tag(html, "strong", sw_html_attr_items(sw_html_attr_kv("data-component", "feature-title")));
    sw_html_text_tr(html, feature->title);
    sw_html_close_tag(html, "strong");
    sw_html_text(html, feature->body);
    sw_html_close_tag(html, "li");
}

static void render_feature_list(sw_html_buffer* html) {
    sz i;

    sw_html_open_tag(html, "ul", sw_html_attr_items(
        sw_html_attr_kv("class", "sw-list"),
        sw_html_attr_kv("data-component", "feature-list")
    ));
    for (i = 0; i < sizeof(sw_example_features) / sizeof(sw_example_features[0]); ++i) {
        render_feature(html, &sw_example_features[i]);
    }
    sw_html_close_tag(html, "ul");
}

static void render_search_demo(sw_html_buffer* html) {
    sw_html_open_tag(html, "p", NULL, 0);
    sw_html_text_tr(html, "Search");
    sw_html_raw(html, " helper with safe HTML escaping and a reusable server library.");
    sw_html_close_tag(html, "p");

    sw_html_void_tag(html, "input", sw_html_attr_items(
        sw_html_attr_kv("type", "text"),
        sw_html_attr_kv("name", "q"),
        sw_html_attr_kv_tr("placeholder", "Search"),
        sw_html_attr_kv("value", "Search"),
        sw_html_attr_kv("data-role", "search")
    ));
}

static void render_root(sw_connection* connection, sw_example_state* state) {
    sw_html_buffer* html = sw_html_buffer_create();

    sw_html_buffer_set_translator(html, state->translator);

    sw_html_raw(html, "<!doctype html>");
    sw_html_open_tag(html, "html", sw_html_attr_items(
        sw_html_attr_kv("lang", sw_translator_get_language(state->translator)),
        sw_html_attr_kv("data-app", "syphax-web")
    ));
    sw_html_open_tag(html, "head", NULL, 0);
    sw_html_meta_charset(html, "utf-8");
    sw_html_title_tr(html, "Syphax Web");
    sw_html_void_tag(html, "link", sw_html_attr_items(
        sw_html_attr_kv("rel", "stylesheet"),
        sw_html_attr_kv("href", "/style.css")
    ));
    sw_html_close_tag(html, "head");

    sw_html_open_tag(html, "body", sw_html_attr_items(sw_html_attr_kv("class", "sw-body")));
    sw_html_open_tag(html, "main", sw_html_attr_items(
        sw_html_attr_kv("class", "sw-shell"),
        sw_html_attr_kv("data-component", "page-shell")
    ));
    sw_html_open_tag(html, "h1", NULL, 0);
    sw_html_text_tr(html, "Syphax Web");
    sw_html_close_tag(html, "h1");

    render_search_demo(html);
    render_feature_list(html);
    sw_html_open_tag(html, "p", NULL, 0);
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
            render_stylesheet(connection);
            return;
        }
    }

    sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "Not Found");
}

int main(void) {
    sw_example_state state;

    state.translator = sw_translator_create();
    sw_translator_set_language(state.translator, "en");

    printf("Listening on 0.0.0.0:8000\n");
    printf("Open http://127.0.0.1:8000 in your browser\n");
    sw_server_listen("http://0.0.0.0:8000", http_handler, &state);

    sw_translator_destroy(state.translator);
    return 0;
}
