#include "sw_html.h"
#include "sw_js.h"
#include "sw_server.h"
#include "sw_translator.h"
#include "sw_utility.h"

#include <stdio.h>
#include <string.h>

#ifndef SYPHAX_WEB_SOURCE_DIR
#    define SYPHAX_WEB_SOURCE_DIR "."
#endif

typedef struct {
    const c8* title;
    const c8* body;
} sw_example_feature;

static const c8 sw_example_translation_catalog[] = "resources/translations.json";

static const sw_example_feature sw_example_features[] = {
    { "Live Search", "One helper emits debounced in-page updates without a separate JavaScript asset." },
    { "HTTP Helpers", "Query fields and routes stay readable without manual buffer clearing in every handler." },
    { "Render Functions", "Pages are easier to copy when markup is split into small focused C functions." }
};

static const sw_language sw_example_languages[] = {
    { .code = "en", .label = "English", .direction = SW_LANGUAGE_DIRECTION_LTR },
    { .code = "ar", .label = "Arabic", .direction = SW_LANGUAGE_DIRECTION_RTL },
    { .code = "fa", .label = "Farsi", .direction = SW_LANGUAGE_DIRECTION_RTL },
    { .code = "zh", .label = "Chinese", .direction = SW_LANGUAGE_DIRECTION_LTR },
    { .code = "ja", .label = "Japanese", .direction = SW_LANGUAGE_DIRECTION_LTR }
};
#define SW_EXAMPLE_LANGUAGE_COUNT ((sz)(sizeof(sw_example_languages) / sizeof(sw_example_languages[0])))

static void resource_path(c8* buffer, sz buffer_len, const c8* relative_path);
static void register_languages(sw_translator* translator);
static const sw_language* find_language(const sw_language* language_list, sz language_list_count, const c8* code);
static const sw_language* select_language(sw_translator* translator, const sw_language* language_list, sz language_list_count, const c8* requested);
static sw_language_direction preview_result_direction(const sw_language* current_language);
static b8 feature_matches_query(const sw_example_feature* feature, const c8* query, const sw_translator* translator);
static sz render_feature_list(sw_buffer* h, const c8* query, const sw_translator* translator);
static void render_preview_status(sw_buffer* h, const c8* query, sz match_count);
static void render_language_switcher(sw_buffer* h, const sw_language* language_list, sz language_list_count, const sw_language* current_language, const c8* query);

static void resource_path(c8* buffer, sz buffer_len, const c8* relative_path) {
    (void)snprintf(buffer, buffer_len, "%s/%s", SYPHAX_WEB_SOURCE_DIR, relative_path);
}

static void register_languages(sw_translator* translator) {
    sw_add_language(translator, .code = "ar", .label = "Arabic", .direction = SW_LANGUAGE_DIRECTION_RTL);
    sw_add_language(translator, .code = "fa", .label = "Farsi", .direction = SW_LANGUAGE_DIRECTION_RTL);
    sw_add_language(translator, .code = "zh", .label = "Chinese", .direction = SW_LANGUAGE_DIRECTION_LTR);
    sw_add_language(translator, .code = "ja", .label = "Japanese", .direction = SW_LANGUAGE_DIRECTION_LTR);
}

static const sw_language* find_language(const sw_language* language_list, sz language_list_count, const c8* code) {
    sz i;

    if (code != NULL && code[0] != '\0') {
        for (i = 0; i < language_list_count; ++i) {
            if (strcmp(language_list[i].code, code) == 0) {
                return &language_list[i];
            }
        }
    }

    return &language_list[0];
}

static const sw_language* select_language(
    sw_translator* translator,
    const sw_language* language_list,
    sz language_list_count,
    const c8* requested
) {
    const sw_language* language = find_language(language_list, language_list_count, requested);

    (void)sw_translator_set_language(translator, language->code);
    return language;
}

static sw_language_direction preview_result_direction(const sw_language* current_language) {
    if (current_language == NULL || current_language->code == NULL) {
        return SW_LANGUAGE_DIRECTION_LTR;
    }

    if (strcmp(current_language->code, "zh") == 0 || strcmp(current_language->code, "ja") == 0) {
        return SW_LANGUAGE_DIRECTION_TTB;
    }

    return current_language->direction;
}

static void render_stylesheet(sw_connection* connection) {
    char path[1024];

    resource_path(path, sizeof(path), "resources/style.css");
    (void)sw_http_serve_file(connection, path);
}

static void render_feature(sw_buffer* h, const sw_example_feature* feature) {
    sw_li(h, sw_no_attrs, {
        sw_strong(h, sw_attrs(sw_attr("data-component", "feature-title")), {
            sw_text(h, feature->title);
        });
        sw_text(h, feature->body);
    });
}

static b8 feature_matches_query(const sw_example_feature* feature, const c8* query, const sw_translator* translator) {
    const c8* title;
    const c8* body;

    if (query == NULL || query[0] == '\0') {
        return 1;
    }

    title = (translator != NULL) ? sw_translate(translator, feature->title) : feature->title;
    body = (translator != NULL) ? sw_translate(translator, feature->body) : feature->body;

    return sw_matches_query(title, query, 0) || sw_matches_query(body, query, 0);
}

static void render_feature_catalog(sw_buffer* h) {
    sw_section(h, sw_attrs(sw_attr("class", "sw-catalog")), {
        sw_h2(h, sw_no_attrs, {
            sw_text(h, "Library Features");
        });
        render_feature_list(h, NULL, sw_buffer_get_translator(h));
    });
}

static sz render_feature_list(sw_buffer* h, const c8* query, const sw_translator* translator) {
    sz i;
    sz rendered = 0;

    sw_ul(h, sw_attrs(
        sw_attr("class", "sw-list"),
        sw_attr("data-component", "feature-list")
    ), {
        for (i = 0; i < sizeof(sw_example_features) / sizeof(sw_example_features[0]); ++i) {
            if (!feature_matches_query(&sw_example_features[i], query, translator)) {
                continue;
            }
            render_feature(h, &sw_example_features[i]);
            rendered += 1;
        }
    });

    return rendered;
}

static sz count_matching_features(const c8* query, const sw_translator* translator) {
    sz i;
    sz match_count = 0;

    for (i = 0; i < sizeof(sw_example_features) / sizeof(sw_example_features[0]); ++i) {
        if (feature_matches_query(&sw_example_features[i], query, translator)) {
            match_count += 1;
        }
    }

    return match_count;
}

static void render_preview_fragment(sw_buffer* h, const sw_language* current_language, const c8* query) {
    const sw_translator* translator = sw_buffer_get_translator(h);
    const sz match_count = count_matching_features(query, translator);

    sw_section(h, sw_attrs(
        sw_attr("class", "sw-preview-shell"),
        sw_attr("data-component", "search-preview")
    ), {
        sw_h2(h, sw_no_attrs, {
            sw_text(h, "Live Preview");
        });

        render_preview_status(h, query, match_count);
        if (query != NULL && query[0] != '\0' && match_count > 0) {
            sw_div(h, sw_attrs(
                sw_attr("class", "sw-preview-results"),
                sw_attr(sw_direction(preview_result_direction(current_language)))
            ), {
                render_feature_list(h, query, translator);
            });
        }
    });
}

static void render_language_switcher(
    sw_buffer* h,
    const sw_language* language_list,
    sz language_list_count,
    const sw_language* current_language,
    const c8* query
) {
    sz i;

    sw_div(h, sw_attrs(sw_attr("class", "sw-language-bar")), {
        sw_span(h, sw_attrs(sw_attr("class", "sw-field-label")), {
            sw_text(h, "Language");
        });
        sw_div(h, sw_attrs(sw_attr("class", "sw-language-actions"), sw_attr(sw_translation(false))), {
            for (i = 0; i < language_list_count; ++i) {
                const sw_language* language = &language_list[i];

                sw_form(h, sw_attrs(
                    sw_attr("class", "sw-language-form"),
                    sw_attr("action", "/"),
                    sw_attr("method", "get")
                ), {
                    sw_input(h, sw_attrs(
                        sw_attr("type", "hidden"),
                        sw_attr("name", "lang"),
                        sw_attr("value", language->code)
                    ));
                    if (query != NULL && query[0] != '\0') {
                        sw_input(h, sw_attrs(
                            sw_attr("type", "hidden"),
                            sw_attr("name", "q"),
                            sw_attr("value", query)
                        ));
                    }
                    sw_button(h, sw_attrs(
                        sw_attr("class",
                            (strcmp(language->code, current_language->code) == 0)
                                ? "sw-language-button is-active"
                                : "sw-language-button"
                        ),
                        sw_attr("type", "submit")
                    ), {
                        sw_text(h, language->label);
                    });
                });
            }
        });
    });
}

static void render_search_demo(
    sw_buffer* h,
    const sw_language* language_list,
    sz language_list_count,
    const sw_language* current_language,
    const c8* query
) {
    sw_p(h, sw_attrs(sw_attr("class", "sw-search-copy")), {
        sw_text(h, "This example keeps the page logic in C, lets you switch languages, and updates the preview as you type.");
    });

    render_language_switcher(h, language_list, language_list_count, current_language, query);

    sw_form(h, sw_attrs(
        sw_attr("id", "sw-search-form"),
        sw_attr("class", "sw-search-form"),
        sw_attr("action", "/"),
        sw_attr("method", "get"),
        sw_attr_bool("novalidate", 1)
    ), {
        sw_input(h, sw_attrs(
            sw_attr("type", "hidden"),
            sw_attr("name", "lang"),
            sw_attr("value", current_language->code)
        ));
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
                sw_text(h, "Open Full Search Page");
            });
        });
        sw_p(h, sw_attrs(sw_attr("class", "sw-search-hint")), {
            sw_text(h, "Typing updates the preview below, and the selected language stays active for both the page and the live search.");
        });
    });

    sw_div(h, sw_attrs(
        sw_attr("id", "sw-search-preview"),
        sw_attr("class", "sw-preview-region"),
        sw_attr("aria-live", "polite")
    ), {
        render_preview_fragment(h, current_language, query);
    });
}

static void render_preview_status(sw_buffer* h, const c8* query, sz match_count) {
    if (query == NULL || query[0] == '\0') {
        sw_div(h, sw_attrs(sw_attr("class", "sw-empty-state")), {
            sw_text(h, "Type in the search field to preview matching feature cards here.");
        });
        return;
    }

    sw_div(h, sw_attrs(sw_attr("class", "sw-result-meta")), {
        sw_span(h, sw_attrs(sw_attr("class", "sw-chip")), {
            sw_rawf(h, "%zu", match_count);
        });
        sw_span(h, sw_no_attrs, {
            sw_text(h, "Matching features for the current query.");
        });
    });

    if (match_count == 0) {
        sw_div(h, sw_attrs(sw_attr("class", "sw-empty-state")), {
            sw_text(h, "No matching example features.");
        });
    }
}

static void render_search_preview(
    sw_connection* connection,
    const sw_translator* translator,
    const sw_language* current_language,
    const c8* query
) {
    sw_buffer* h = sw_buffer_new();

    sw_buffer_set_translator(h, translator);
    render_preview_fragment(h, current_language, query);
    (void)sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
    sw_buffer_free(h);
}

static void render_root(
    sw_connection* connection,
    const sw_translator* translator,
    const sw_language* language_list,
    sz language_list_count,
    const sw_language* current_language,
    const c8* query
) {
    sw_buffer* h = sw_buffer_new();

    sw_buffer_set_translator(h, translator);
    sw_html(h, sw_attrs(
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

                render_search_demo(h, language_list, language_list_count, current_language, query);
                (void)sw_js_live_search(h, "sw-search-form", "sw-search-query", "sw-search-preview", "/search-preview");
                render_feature_catalog(h);
                sw_p(h, sw_no_attrs, {
                    sw_text(h, "Static assets are served from the resources directory through the same library API.");
                });
            });
        });
    });

    (void)sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
    sw_buffer_free(h);
}

static void http_handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    sw_translator* translator = (sw_translator*)user_data;
    const sw_language* current_language;
    char query[256];
    char language_code[32];

    (void)sw_http_get_query(request, "lang", language_code, sizeof(language_code));
    current_language = select_language(translator, sw_example_languages, SW_EXAMPLE_LANGUAGE_COUNT, language_code);

    if (sw_http_is(request, "GET", "/")) {
        (void)sw_http_get_query(request, "q", query, sizeof(query));
        render_root(connection, translator, sw_example_languages, SW_EXAMPLE_LANGUAGE_COUNT, current_language, query);
        return;
    }
    if (sw_http_is(request, "GET", "/style.css")) {
        render_stylesheet(connection);
        return;
    }
    if (sw_http_is(request, "GET", "/search-preview")) {
        (void)sw_http_get_query(request, "q", query, sizeof(query));
        render_search_preview(connection, translator, current_language, query);
        return;
    }

    (void)sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "Not Found");
}

int main(void) {
    sw_translator* translator;
    char catalog_path[1024];
    i32 rc;

    translator = sw_translator_create(
        .code = sw_example_languages[0].code,
        .label = sw_example_languages[0].label,
        .direction = sw_example_languages[0].direction
    );
    register_languages(translator);
    resource_path(catalog_path, sizeof(catalog_path), sw_example_translation_catalog);
    (void)sw_translator_load_catalog_all_json_file(translator, catalog_path);
    (void)sw_translator_set_language(translator, "en");

    printf("Listening on 0.0.0.0:8000\n");
    printf("Open http://127.0.0.1:8000 in your browser\n");
    rc = sw_server_listen("http://0.0.0.0:8000", http_handler, translator);
    sw_translator_destroy(translator);
    return rc;
}
