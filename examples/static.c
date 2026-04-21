#include "sw_html.h"
#include "sw_js.h"
#include "sw_server.h"
#include "sw_translator.h"

#include <ctype.h>
#include <stdio.h>
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

static void render_preview_status(sw_html_buffer* html, const c8* query, sz match_count);

static void render_stylesheet(sw_connection* connection) {
    sw_http_serve_file(connection, "resources/style.css");
}

static b8 sw_contains_case_insensitive(const c8* haystack, const c8* needle) {
    sz haystack_index;
    sz needle_index;
    sz haystack_len;
    sz needle_len;

    if (haystack == NULL || needle == NULL) {
        return 0;
    }

    needle_len = strlen(needle);
    haystack_len = strlen(haystack);

    if (needle_len == 0) {
        return 1;
    }

    if (needle_len > haystack_len) {
        return 0;
    }

    for (haystack_index = 0; haystack_index + needle_len <= haystack_len; ++haystack_index) {
        for (needle_index = 0; needle_index < needle_len; ++needle_index) {
            if (tolower((unsigned char)haystack[haystack_index + needle_index])
                != tolower((unsigned char)needle[needle_index])) {
                break;
            }
        }
        if (needle_index == needle_len) {
            return 1;
        }
    }

    return 0;
}

static b8 sw_feature_matches_query(const sw_example_feature* feature, const c8* query) {
    return sw_contains_case_insensitive(feature->title, query)
        || sw_contains_case_insensitive(feature->body, query);
}

static void render_feature(sw_html_buffer* html, const sw_example_feature* feature) {
    sw_html_open_tag(html, "li", NULL, 0);
    sw_html_open_tag(html, "strong", sw_html_attr_items(sw_html_attr_kv("data-component", "feature-title")));
    sw_html_text_tr(html, feature->title);
    sw_html_close_tag(html, "strong");
    sw_html_text(html, feature->body);
    sw_html_close_tag(html, "li");
}

static sz render_feature_list(sw_html_buffer* html, const c8* query) {
    sz i;
    sz rendered = 0;

    sw_html_open_tag(html, "ul", sw_html_attr_items(
        sw_html_attr_kv("class", "sw-list"),
        sw_html_attr_kv("data-component", "feature-list")
    ));
    for (i = 0; i < sizeof(sw_example_features) / sizeof(sw_example_features[0]); ++i) {
        if (query != NULL && query[0] != '\0' && !sw_feature_matches_query(&sw_example_features[i], query)) {
            continue;
        }
        render_feature(html, &sw_example_features[i]);
        rendered += 1;
    }
    sw_html_close_tag(html, "ul");
    return rendered;
}

static sz count_matching_features(const c8* query) {
    sz i;
    sz match_count = 0;

    for (i = 0; i < sizeof(sw_example_features) / sizeof(sw_example_features[0]); ++i) {
        if (query == NULL || query[0] == '\0' || sw_feature_matches_query(&sw_example_features[i], query)) {
            match_count += 1;
        }
    }

    return match_count;
}

static void render_preview_fragment(sw_html_buffer* html, const c8* query) {
    const sz match_count = count_matching_features(query);

    sw_html_open_tag(html, "section", sw_html_attr_items(
        sw_html_attr_kv("class", "sw-preview-shell"),
        sw_html_attr_kv("data-component", "search-preview")
    ));
    sw_html_open_tag(html, "h2", NULL, 0);
    sw_html_text_tr(html, "Search");
    sw_html_close_tag(html, "h2");

    render_preview_status(html, query, match_count);
    if (query != NULL && query[0] != '\0' && match_count > 0) {
        sw_html_open_tag(html, "div", sw_html_attr_items(sw_html_attr_kv("class", "sw-preview-results")));
        render_feature_list(html, query);
        sw_html_close_tag(html, "div");
    }
    sw_html_close_tag(html, "section");
}

static void render_search_demo(sw_html_buffer* html, const c8* query) {
    sw_html_open_tag(html, "p", sw_html_attr_items(sw_html_attr_kv("class", "sw-search-copy")));
    sw_html_text_tr(html, "Search");
    sw_html_raw(html, " updates an inline preview as you type, using reusable helpers emitted directly from the library.");
    sw_html_close_tag(html, "p");

    sw_html_open_tag(html, "form", sw_html_attr_items(
        sw_html_attr_kv("id", "sw-search-form"),
        sw_html_attr_kv("class", "sw-search-form"),
        sw_html_attr_kv("action", "/"),
        sw_html_attr_kv("method", "post"),
        sw_html_attr_bool("novalidate", 1)
    ));
    sw_html_open_tag(html, "label", sw_html_attr_items(
        sw_html_attr_kv("class", "sw-field-label"),
        sw_html_attr_kv("for", "sw-search-query")
    ));
    sw_html_text_tr(html, "Search");
    sw_html_close_tag(html, "label");
    sw_html_void_tag(html, "input", sw_html_attr_items(
        sw_html_attr_kv("id", "sw-search-query"),
        sw_html_attr_kv("class", "sw-search-input"),
        sw_html_attr_kv("type", "text"),
        sw_html_attr_kv("name", "q"),
        sw_html_attr_kv_tr("placeholder", "Search"),
        sw_html_attr_kv("value", (query != NULL) ? query : ""),
        sw_html_attr_kv("autocomplete", "off"),
        sw_html_attr_kv("spellcheck", "false")
    ));
    sw_html_open_tag(html, "p", sw_html_attr_items(sw_html_attr_kv("class", "sw-search-hint")));
    sw_html_raw(html, "Type to filter the feature list below. The page emits its live-search behavior from C, and pressing Enter still works without JavaScript.");
    sw_html_close_tag(html, "p");
    sw_html_close_tag(html, "form");

    sw_html_open_tag(html, "div", sw_html_attr_items(
        sw_html_attr_kv("id", "sw-search-preview"),
        sw_html_attr_kv("class", "sw-preview-region"),
        sw_html_attr_kv("aria-live", "polite")
    ));
    render_preview_fragment(html, query);
    sw_html_close_tag(html, "div");
}

static void render_preview_status(sw_html_buffer* html, const c8* query, sz match_count) {
    if (query == NULL || query[0] == '\0') {
        sw_html_open_tag(html, "div", sw_html_attr_items(sw_html_attr_kv("class", "sw-empty-state")));
        sw_html_text(html, "Type in the search field to preview matching feature cards here.");
        sw_html_close_tag(html, "div");
        return;
    }

    sw_html_open_tag(html, "div", sw_html_attr_items(sw_html_attr_kv("class", "sw-result-meta")));
    sw_html_open_tag(html, "span", sw_html_attr_items(sw_html_attr_kv("class", "sw-chip")));
    sw_html_rawf(html, "%zu match%s", match_count, (match_count == 1) ? "" : "es");
    sw_html_close_tag(html, "span");
    sw_html_open_tag(html, "span", NULL, 0);
    sw_html_text(html, "Query:");
    sw_html_raw(html, " ");
    sw_html_open_tag(html, "code", sw_html_attr_items(sw_html_attr_kv("class", "sw-search-term")));
    sw_html_text(html, query);
    sw_html_close_tag(html, "code");
    sw_html_close_tag(html, "span");
    sw_html_close_tag(html, "div");

    if (match_count == 0) {
        sw_html_open_tag(html, "div", sw_html_attr_items(sw_html_attr_kv("class", "sw-empty-state")));
        sw_html_text(html, "No matching example features.");
        sw_html_close_tag(html, "div");
    }
}

static void render_search_preview(sw_connection* connection, sw_example_state* state, const c8* query) {
    sw_html_buffer* html = sw_html_buffer_create();

    sw_html_buffer_set_translator(html, state->translator);
    render_preview_fragment(html, query);
    sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_html_buffer_data(html), sw_html_buffer_size(html));
    sw_html_buffer_destroy(html);
}

static void render_root(sw_connection* connection, sw_example_state* state, const c8* query) {
    sw_html_buffer* html = sw_html_buffer_create();
    const sw_js_live_search_options live_search = {
        .form_id = "sw-search-form",
        .input_id = "sw-search-query",
        .target_id = "sw-search-preview",
        .endpoint = "/search-preview",
        .value_param = "q",
        .loading_class = "is-loading",
        .debounce_ms = 120,
        .method = SW_JS_HTTP_POST,
        .swap_mode = SW_JS_SWAP_INNER_HTML,
        .serialize_form = 1,
        .abort_stale = 1,
        .prevent_submit = 1
    };

    sw_html_buffer_set_translator(html, state->translator);

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

    render_search_demo(html, query);
    sw_js_live_search(html, &live_search);
    render_feature_list(html, NULL);
    sw_html_open_tag(html, "p", NULL, 0);
    sw_html_raw(html, "Static assets are served from the resources directory through the same library API.");
    sw_html_close_tag(html, "p");
    sw_html_close_tag(html, "main");
    sw_html_close_tag(html, "body");
    sw_html_close_tag(html, "html");

    sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_html_buffer_data(html), sw_html_buffer_size(html));
    sw_html_buffer_destroy(html);
}

static void http_handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    sw_example_state* state = (sw_example_state*)user_data;
    char query[256];

    if (strcmp(request->method, "GET") == 0) {
        if (strcmp(request->uri, "/") == 0) {
            render_root(connection, state, "");
            return;
        }
        if (strcmp(request->uri, "/style.css") == 0) {
            render_stylesheet(connection);
            return;
        }
        if (strcmp(request->uri, "/search-preview") == 0) {
            render_search_preview(connection, state, "");
            return;
        }
    }

    if (strcmp(request->method, "POST") == 0 && strcmp(request->uri, "/search-preview") == 0) {
        query[0] = '\0';
        if (sw_http_get_var(request, "q", query, sizeof(query)) < 0) {
            query[0] = '\0';
        }
        render_search_preview(connection, state, query);
        return;
    }

    if (strcmp(request->method, "POST") == 0 && strcmp(request->uri, "/") == 0) {
        query[0] = '\0';
        if (sw_http_get_var(request, "q", query, sizeof(query)) < 0) {
            query[0] = '\0';
        }
        render_root(connection, state, query);
        return;
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
