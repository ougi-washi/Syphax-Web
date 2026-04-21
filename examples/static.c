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

static void render_preview_status(sw_hbuf* h, const c8* query, sz match_count);

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

static void render_feature(sw_hbuf* h, const sw_example_feature* feature) {
    sw_li(h, sw_no_attrs, {
        sw_strong(h, sw_attrs(sw_kv("data-component", "feature-title")), {
            sw_txt_tr(h, feature->title);
        });
        sw_txt(h, feature->body);
    });
}

static sz render_feature_list(sw_hbuf* h, const c8* query) {
    sz i;
    sz rendered = 0;

    sw_ul(h, sw_attrs(
        sw_kv("class", "sw-list"),
        sw_kv("data-component", "feature-list")
    ), {
        for (i = 0; i < sizeof(sw_example_features) / sizeof(sw_example_features[0]); ++i) {
            if (query != NULL && query[0] != '\0' && !sw_feature_matches_query(&sw_example_features[i], query)) {
                continue;
            }
            render_feature(h, &sw_example_features[i]);
            rendered += 1;
        }
    });

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

static void render_preview_fragment(sw_hbuf* h, const c8* query) {
    const sz match_count = count_matching_features(query);

    sw_section(h, sw_attrs(
        sw_kv("class", "sw-preview-shell"),
        sw_kv("data-component", "search-preview")
    ), {
        sw_h2(h, sw_no_attrs, {
            sw_txt_tr(h, "Search");
        });

        render_preview_status(h, query, match_count);
        if (query != NULL && query[0] != '\0' && match_count > 0) {
            sw_div(h, sw_attrs(sw_kv("class", "sw-preview-results")), {
                render_feature_list(h, query);
            });
        }
    });
}

static void render_search_demo(sw_hbuf* h, const c8* query) {
    sw_p(h, sw_attrs(sw_kv("class", "sw-search-copy")), {
        sw_txt_tr(h, "Search");
        sw_raw(h, " updates an inline preview as you type, using reusable helpers emitted directly from the library.");
    });

    sw_form(h, sw_attrs(
        sw_kv("id", "sw-search-form"),
        sw_kv("class", "sw-search-form"),
        sw_kv("action", "/"),
        sw_kv("method", "post"),
        sw_bool("novalidate", 1)
    ), {
        sw_label(h, sw_attrs(
            sw_kv("class", "sw-field-label"),
            sw_kv("for", "sw-search-query")
        ), {
            sw_txt_tr(h, "Search");
        });
        sw_input(h, sw_attrs(
            sw_kv("id", "sw-search-query"),
            sw_kv("class", "sw-search-input"),
            sw_kv("type", "text"),
            sw_kv("name", "q"),
            sw_tr("placeholder", "Search"),
            sw_kv("value", (query != NULL) ? query : ""),
            sw_kv("autocomplete", "off"),
            sw_kv("spellcheck", "false")
        ));
        sw_p(h, sw_attrs(sw_kv("class", "sw-search-hint")), {
            sw_raw(h, "Type to filter the feature list below. The page emits its live-search behavior from C, and pressing Enter still works without JavaScript.");
        });
    });

    sw_div(h, sw_attrs(
        sw_kv("id", "sw-search-preview"),
        sw_kv("class", "sw-preview-region"),
        sw_kv("aria-live", "polite")
    ), {
        render_preview_fragment(h, query);
    });
}

static void render_preview_status(sw_hbuf* h, const c8* query, sz match_count) {
    if (query == NULL || query[0] == '\0') {
        sw_div(h, sw_attrs(sw_kv("class", "sw-empty-state")), {
            sw_txt(h, "Type in the search field to preview matching feature cards here.");
        });
        return;
    }

    sw_div(h, sw_attrs(sw_kv("class", "sw-result-meta")), {
        sw_span(h, sw_attrs(sw_kv("class", "sw-chip")), {
            sw_rawf(h, "%zu match%s", match_count, (match_count == 1) ? "" : "es");
        });
        sw_span(h, sw_no_attrs, {
            sw_txt(h, "Query:");
            sw_raw(h, " ");
            sw_code(h, sw_attrs(sw_kv("class", "sw-search-term")), {
                sw_txt(h, query);
            });
        });
    });

    if (match_count == 0) {
        sw_div(h, sw_attrs(sw_kv("class", "sw-empty-state")), {
            sw_txt(h, "No matching example features.");
        });
    }
}

static void render_search_preview(sw_connection* connection, sw_example_state* state, const c8* query) {
    sw_hbuf* h = sw_hbuf_new();

    sw_hbuf_set_tr(h, state->translator);
    render_preview_fragment(h, query);
    sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_hbuf_data(h), sw_hbuf_len(h));
    sw_hbuf_free(h);
}

static void render_root(sw_connection* connection, sw_example_state* state, const c8* query) {
    sw_hbuf* h = sw_hbuf_new();

    sw_hbuf_set_tr(h, state->translator);

    sw_html(h, sw_attrs(
        sw_kv("lang", sw_translator_get_language(state->translator)),
        sw_kv("data-app", "syphax-web")
    ), {
        sw_head(h, sw_no_attrs, {
            sw_meta_charset(h, "utf-8");
            sw_title_tr(h, "Syphax Web");
            sw_link(h, sw_attrs(
                sw_kv("rel", "stylesheet"),
                sw_kv("href", "/style.css")
            ));
        });

        sw_body(h, sw_attrs(sw_kv("class", "sw-body")), {
            sw_main(h, sw_attrs(
                sw_kv("class", "sw-shell"),
                sw_kv("data-component", "page-shell")
            ), {
                sw_h1(h, sw_no_attrs, {
                    sw_txt_tr(h, "Syphax Web");
                });

                render_search_demo(h, query);
                sw_j_live(h,
                    .form_id = "sw-search-form",
                    .input_id = "sw-search-query",
                    .target_id = "sw-search-preview",
                    .endpoint = "/search-preview",
                    .value_param = "q",
                    .loading_class = "is-loading",
                    .debounce_ms = 120,
                    .method = SW_J_POST,
                    .swap_mode = SW_J_INNER,
                    .serialize_form = 1,
                    .abort_stale = 1,
                    .prevent_submit = 1
                );
                render_feature_list(h, NULL);
                sw_p(h, sw_no_attrs, {
                    sw_raw(h, "Static assets are served from the resources directory through the same library API.");
                });
            });
        });
    });

    sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_hbuf_data(h), sw_hbuf_len(h));
    sw_hbuf_free(h);
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
