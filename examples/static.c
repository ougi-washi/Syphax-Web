#include "sw_html.h"
#include "sw_js.h"
#include "sw_server.h"
#include "sw_utility.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    const c8* title;
    const c8* body;
} sw_example_feature;

static const sw_example_feature sw_example_features[] = {
    { "Live Search", "One helper emits debounced in-page updates without a separate JavaScript asset." },
    { "HTTP Helpers", "Query fields and routes stay readable without manual buffer clearing in every handler." },
    { "Render Functions", "Pages are easier to copy when markup is split into small focused C functions." }
};

static sz render_feature_list(sw_hbuf* h, const c8* query);
static void render_preview_status(sw_hbuf* h, const c8* query, sz match_count);

static void render_stylesheet(sw_connection* connection) {
    (void)sw_http_serve_file(connection, "resources/style.css");
}

static void render_feature(sw_hbuf* h, const sw_example_feature* feature) {
    sw_li(h, sw_no_attrs, {
        sw_strong(h, sw_attrs(sw_attr("data-component", "feature-title")), {
            sw_text(h, feature->title);
        });
        sw_text(h, feature->body);
    });
}

static void render_feature_catalog(sw_hbuf* h) {
    sw_section(h, sw_attrs(sw_attr("class", "sw-catalog")), {
        sw_h2(h, sw_no_attrs, {
            sw_text(h, "Library Features");
        });
        render_feature_list(h, NULL);
    });
}

static sz render_feature_list(sw_hbuf* h, const c8* query) {
    sz i;
    sz rendered = 0;

    sw_ul(h, sw_attrs(
        sw_attr("class", "sw-list"),
        sw_attr("data-component", "feature-list")
    ), {
        for (i = 0; i < sizeof(sw_example_features) / sizeof(sw_example_features[0]); ++i) {
            if (query != NULL
                && query[0] != '\0'
                && !sw_matches_query(sw_example_features[i].title, query, 0)
                && !sw_matches_query(sw_example_features[i].body, query, 0)) {
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
        if (query == NULL
            || query[0] == '\0'
            || sw_matches_query(sw_example_features[i].title, query, 0)
            || sw_matches_query(sw_example_features[i].body, query, 0)) {
            match_count += 1;
        }
    }

    return match_count;
}

static void render_preview_fragment(sw_hbuf* h, const c8* query) {
    const sz match_count = count_matching_features(query);

    sw_section(h, sw_attrs(
        sw_attr("class", "sw-preview-shell"),
        sw_attr("data-component", "search-preview")
    ), {
        sw_h2(h, sw_no_attrs, {
            sw_text(h, "Live Preview");
        });

        render_preview_status(h, query, match_count);
        if (query != NULL && query[0] != '\0' && match_count > 0) {
            sw_div(h, sw_attrs(sw_attr("class", "sw-preview-results")), {
                render_feature_list(h, query);
            });
        }
    });
}

static void render_search_demo(sw_hbuf* h, const c8* query) {
    sw_p(h, sw_attrs(sw_attr("class", "sw-search-copy")), {
        sw_text(h, "This example keeps the page logic in C and updates the preview as you type.");
    });

    sw_form(h, sw_attrs(
        sw_attr("id", "sw-search-form"),
        sw_attr("class", "sw-search-form"),
        sw_attr("action", "/"),
        sw_attr("method", "get"),
        sw_attr_bool("novalidate", 1)
    ), {
        sw_label(h, sw_attrs(
            sw_attr("class", "sw-field-label"),
            sw_attr("for", "sw-search-query")
        ), {
            sw_text(h, "Search");
        });
        sw_input(h, sw_attrs(
            sw_attr("id", "sw-search-query"),
            sw_attr("class", "sw-search-input"),
            sw_attr("type", "text"),
            sw_attr("name", "q"),
            sw_attr("placeholder", "Search"),
            sw_attr("value", (query != NULL) ? query : ""),
            sw_attr("autocomplete", "off"),
            sw_attr("spellcheck", "false")
        ));
        sw_div(h, sw_attrs(sw_attr("class", "sw-search-actions")), {
            sw_button(h, sw_attrs(
                sw_attr("class", "sw-search-button"),
                sw_attr("type", "submit")
            ), {
                sw_text(h, "Open Search Page");
            });
        });
        sw_p(h, sw_attrs(sw_attr("class", "sw-search-hint")), {
            sw_text(h, "Typing updates the preview below, and submitting the form still works without JavaScript.");
        });
    });

    sw_div(h, sw_attrs(
        sw_attr("id", "sw-search-preview"),
        sw_attr("class", "sw-preview-region"),
        sw_attr("aria-live", "polite")
    ), {
        render_preview_fragment(h, query);
    });
}

static void render_preview_status(sw_hbuf* h, const c8* query, sz match_count) {
    if (query == NULL || query[0] == '\0') {
        sw_div(h, sw_attrs(sw_attr("class", "sw-empty-state")), {
            sw_text(h, "Type in the search field to preview matching feature cards here.");
        });
        return;
    }

    sw_div(h, sw_attrs(sw_attr("class", "sw-result-meta")), {
        sw_span(h, sw_attrs(sw_attr("class", "sw-chip")), {
            sw_rawf(h, "%zu match%s", match_count, (match_count == 1) ? "" : "es");
        });
        sw_span(h, sw_no_attrs, {
            sw_text(h, "Filtering the example feature list with the current query.");
        });
    });

    if (match_count == 0) {
        sw_div(h, sw_attrs(sw_attr("class", "sw-empty-state")), {
            sw_text(h, "No matching example features.");
        });
    }
}

static void render_search_preview(sw_connection* connection, const c8* query) {
    sw_hbuf* h = sw_hbuf_new();

    render_preview_fragment(h, query);
    (void)sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_hbuf_data(h), sw_hbuf_len(h));
    sw_hbuf_free(h);
}

static void render_root(sw_connection* connection, const c8* query) {
    sw_hbuf* h = sw_hbuf_new();

    sw_html(h, sw_attrs(
        sw_attr("lang", "en"),
        sw_attr("data-app", "syphax-web")
    ), {
        sw_head(h, sw_no_attrs, {
            sw_meta_charset(h, "utf-8");
            sw_title(h, "Syphax Web");
            sw_link(h, sw_attrs(
                sw_attr("rel", "stylesheet"),
                sw_attr("href", "/style.css")
            ));
        });

        sw_body(h, sw_attrs(sw_attr("class", "sw-body")), {
            sw_main(h, sw_attrs(
                sw_attr("class", "sw-shell"),
                sw_attr("data-component", "page-shell")
            ), {
                sw_h1(h, sw_no_attrs, {
                    sw_text(h, "Syphax Web");
                });

                render_search_demo(h, query);
                (void)sw_j_live_search(h, "sw-search-form", "sw-search-query", "sw-search-preview", "/search-preview");
                render_feature_catalog(h);
                sw_p(h, sw_no_attrs, {
                    sw_text(h, "Static assets are served from the resources directory through the same library API.");
                });
            });
        });
    });

    (void)sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_hbuf_data(h), sw_hbuf_len(h));
    sw_hbuf_free(h);
}

static void http_handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    char query[256];

    (void)user_data;

    if (sw_http_is(request, "GET", "/")) {
        (void)sw_http_get_query(request, "q", query, sizeof(query));
        render_root(connection, query);
        return;
    }
    if (sw_http_is(request, "GET", "/style.css")) {
        render_stylesheet(connection);
        return;
    }
    if (sw_http_is(request, "GET", "/search-preview")) {
        (void)sw_http_get_query(request, "q", query, sizeof(query));
        render_search_preview(connection, query);
        return;
    }

    (void)sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "Not Found");
}

int main(void) {
    printf("Listening on 0.0.0.0:8000\n");
    printf("Open http://127.0.0.1:8000 in your browser\n");
    return sw_server_listen("http://0.0.0.0:8000", http_handler, NULL);
}
