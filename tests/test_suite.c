#include "sw_html.h"
#include "sw_js.h"
#include "sw_server.h"
#include "sw_translator.h"
#include "sw_utility.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SYPHAX_WEB_SOURCE_DIR
#    define SYPHAX_WEB_SOURCE_DIR "."
#endif

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

static sz count_occurrences(const char* haystack, const char* needle) {
    sz count = 0;
    const char* cursor = haystack;
    const sz needle_len = strlen(needle);

    while (cursor != NULL && *cursor != '\0') {
        cursor = strstr(cursor, needle);
        if (cursor == NULL) {
            break;
        }
        count += 1;
        cursor += needle_len;
    }

    return count;
}

static void repo_path(char* buffer, sz buffer_len, const char* relative_path) {
    assert(buffer != NULL);
    assert(buffer_len > 0);
    assert(relative_path != NULL);
    assert(snprintf(buffer, buffer_len, "%s/%s", SYPHAX_WEB_SOURCE_DIR, relative_path) > 0);
}

static void translation_catalog_path(char* buffer, sz buffer_len) {
    repo_path(buffer, buffer_len, "resources/translations.json");
}

static void load_fixture_catalog(sw_translator* translator) {
    char path[1024];

    sw_add_language(translator, .code = "fr", .label = "French", .direction = SW_LANGUAGE_DIRECTION_LTR);
    sw_add_language(translator, .code = "ar", .label = "Arabic", .direction = SW_LANGUAGE_DIRECTION_RTL);
    sw_add_language(translator, .code = "fa", .label = "Farsi", .direction = SW_LANGUAGE_DIRECTION_RTL);
    sw_add_language(translator, .code = "zh", .label = "Chinese", .direction = SW_LANGUAGE_DIRECTION_LTR);
    sw_add_language(translator, .code = "ja", .label = "Japanese", .direction = SW_LANGUAGE_DIRECTION_LTR);

    translation_catalog_path(path, sizeof(path));
    assert(sw_translator_load_catalog_all_json_file(translator, path));
}

static void sw_test_handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    sw_test_server_state* state = (sw_test_server_state*)user_data;
    char query[128];

    state->request_count += 1;

    if (sw_http_is(request, "GET", "/")) {
        sw_buffer* h = sw_buffer_new();
        assert(sw_http_get_query(request, "q", query, sizeof(query)) >= 0);
        sw_html(h, sw_no_attrs, {
            sw_body(h, sw_no_attrs, {
                sw_h1(h, sw_no_attrs, {
                    sw_text(h, "Syphax Web");
                });
                sw_div(h, sw_attrs(sw_attr("id", "sw-search-preview")), {
                    sw_section(h, sw_attrs(sw_attr("class", "sw-preview-shell")), {
                        sw_p(h, sw_no_attrs, {
                            if (query[0] == '\0') {
                                sw_text(h, "Type to search.");
                            } else {
                                sw_text_no_translate(h, query);
                            }
                        });
                    });
                });
            });
        });
        sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
        sw_buffer_free(h);
        return;
    }

    if (sw_http_is(request, "POST", "/form")) {
        assert(sw_http_get_form(request, "name", query, sizeof(query)) > 0);
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "name=%s", query);
        return;
    }

    if (sw_http_is(request, "GET", "/file")) {
        assert(sw_http_serve_file(connection, state->file_path) == 0);
        return;
    }

    if (sw_http_is(request, "GET", "/style.css")) {
        const c8 css[] = "body { color: #fff; }\n";
        assert(sw_http_reply(connection, 200, "text/css; charset=utf-8", css, sizeof(css) - 1) == 0);
        return;
    }

    if (sw_http_is(request, "GET", "/search-preview")) {
        sw_buffer* h = sw_buffer_new();
        assert(h != NULL);
        assert(sw_http_get_query(request, "q", query, sizeof(query)) >= 0);
        sw_section(h, sw_attrs(sw_attr("class", "sw-preview-shell")), {
            sw_p(h, sw_no_attrs, {
                if (query[0] == '\0') {
                    sw_text(h, "Type to search.");
                } else {
                    sw_text_no_translate(h, query);
                }
            });
        });
        assert(sw_http_reply(connection, 200, "text/html; charset=utf-8",
            sw_buffer_data(h), sw_buffer_len(h)) == 0);
        sw_buffer_free(h);
        return;
    }

    sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "Not Found");
}

static void test_translator(void) {
    sw_translator* translator = sw_translator_create(.code = "en", .label = "English", .direction = SW_LANGUAGE_DIRECTION_LTR);
    char catalog_path[1024];

    assert(translator != NULL);
    assert(strcmp(sw_translator_get_language(translator), "en") == 0);
    assert(!sw_translator_set_language(translator, "fr"));

    sw_add_language(translator, .code = "de", .label = "German", .direction = SW_LANGUAGE_DIRECTION_LTR);
    assert(sw_translator_set_language(translator, "de"));
    assert(strcmp(sw_translator_get_language(translator), "de") == 0);
    assert(strcmp(sw_translate(translator, "Search"), "Search") == 0);
    assert(sw_translator_set_language(translator, "en"));

    translation_catalog_path(catalog_path, sizeof(catalog_path));

    assert(sw_translator_load_catalog_all_json_file(translator, catalog_path));
    assert(sw_translator_set_language(translator, "en"));
    assert(strcmp(sw_translate(translator, "Search"), "Search") == 0);
    assert(sw_translator_set_language(translator, "fr"));
    assert(strcmp(sw_translator_get_language(translator), "fr") == 0);
    assert(strcmp(sw_translate(translator, "Search"), "Rechercher") == 0);
    assert(strcmp(sw_translate(translator, "Unknown"), "Unknown") == 0);
    assert(!sw_translator_load_catalog_all_json_text(translator, "[\"bad\"]"));
    assert(!sw_translator_load_catalog_all_json_text(translator, "{\"Search\": 1}"));
    assert(!sw_translator_load_catalog_json_file(translator, "fr", "resources/missing-translations.json"));
    assert(strcmp(sw_translate(translator, "Search"), "Rechercher") == 0);

    assert(!sw_translator_load_catalog_json_text(translator, "fr", "[\"bad\"]"));
    assert(!sw_translator_load_catalog_json_text(translator, "fr", "{\"Search\": 1}"));
    assert(!sw_translator_load_catalog_json_text(translator, "fr", "{\"Search\": {\"fr\": 1}}"));
    assert(!sw_translator_load_catalog_json_text(translator, "fr", "{\"Search\": {\"fr\":\"A\",\"fr\":\"B\"}}"));
    assert(!sw_translator_load_catalog_json_text(translator, "fr", "{\"Search\": {\"fr\":\"A\"}, \"Search\": {\"fr\":\"B\"}}"));
    assert(strcmp(sw_translate(translator, "Search"), "Rechercher") == 0);

    assert(sw_translator_set_language(translator, "ja"));
    assert(strcmp(sw_translate(translator, "Search"), "検索") == 0);
    assert(sw_translator_set_language(translator, "ar"));
    assert(strcmp(sw_translate(translator, "Language"), "اللغة") == 0);
    assert(sw_translator_set_language(translator, "fa"));
    assert(strcmp(sw_translate(translator, "Language"), "زبان") == 0);
    assert(sw_translator_set_language(translator, "zh"));
    assert(strcmp(sw_translate(translator, "Language"), "语言") == 0);

    assert(sw_translator_load_catalog_json_text(translator, "fr",
        "{"
            "\"Search\":{\"fr\":\"Chercher\"},"
            "\"Language\":{\"fr\":\"Langage\"}"
        "}"));
    assert(sw_translator_set_language(translator, "fr"));
    assert(strcmp(sw_translate(translator, "Search"), "Chercher") == 0);
    assert(strcmp(sw_translate(translator, "Language"), "Langage") == 0);
    assert(strcmp(sw_translate(translator, "Home"), "Home") == 0);

    assert(sw_translator_load_json_text(translator, "fr",
        "{"
            "\"Search\":\"Trouver\","
            "\"Language\":\"Langue\""
        "}"));
    assert(strcmp(sw_translate(translator, "Search"), "Trouver") == 0);
    assert(strcmp(sw_translate(translator, "Language"), "Langue") == 0);

    assert(sw_translator_set_language(translator, "en"));
    assert(strcmp(sw_translate(translator, "Search"), "Search") == 0);
    sw_translator_destroy(translator);
}

static void test_html_short_api(void) {
    sw_translator* translator = sw_translator_create(.code = "en", .label = "English", .direction = SW_LANGUAGE_DIRECTION_LTR);
    sw_buffer* h = sw_buffer_new();
    const c8* html;

    load_fixture_catalog(translator);
    assert(sw_translator_set_language(translator, "fr"));
    sw_buffer_set_translator(h, translator);

    assert(sw_tag(h, "div", sw_attrs(
        sw_attr("class", "shell"),
        sw_attr("title", "Search"),
        sw_attr("aria-label", "Search"),
        (sw_attr_item){ .name = "alt", .value = "Search", .enabled = 1, .no_translate = 1 }
    )));
    assert(sw_void(h, "input", sw_attrs(
        sw_attr("type", "text"),
        sw_attr("placeholder", "Search"),
        sw_attr("value", "\"quoted\"")
    )));
    assert(sw_text(h, "Search"));
    assert(sw_text_no_translate(h, "Search"));
    assert(sw_end(h, "div"));

    assert(strstr(sw_buffer_data(h), "title=\"Rechercher\"") != NULL);
    assert(strstr(sw_buffer_data(h), "placeholder=\"Rechercher\"") != NULL);
    assert(strstr(sw_buffer_data(h), "aria-label=\"Rechercher\"") != NULL);
    assert(strstr(sw_buffer_data(h), "alt=\"Search\"") != NULL);
    assert(strstr(sw_buffer_data(h), "value=\"&quot;quoted&quot;\"") != NULL);
    assert(strstr(sw_buffer_data(h), "RechercherSearch") != NULL);
    assert(strstr(sw_buffer_data(h), "</input>") == NULL);

    sw_buffer_reset(h);
    assert(sw_buffer_translation_enabled(h));
    sw_translate_off(h);
    assert(!sw_buffer_translation_enabled(h));
    assert(sw_tag(h, "div", sw_attrs(sw_attr("title", "Search"))));
    assert(sw_text(h, "Search"));
    sw_translate_on(h);
    assert(sw_buffer_translation_enabled(h));
    assert(sw_text(h, "Search"));
    assert(sw_end(h, "div"));
    html = sw_buffer_data(h);
    assert(strstr(html, "title=\"Search\"") != NULL);
    assert(strstr(html, ">SearchRechercher</div>") != NULL);

    sw_buffer_reset(h);
    assert(sw_tag(h, "HTML", sw_no_attrs));
    assert(sw_end(h, "HTML"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<!doctype html><HTML") != NULL);
    assert(count_occurrences(html, "<!doctype html>") == 1);

    sw_buffer_reset(h);
    assert(sw_translator_set_language(translator, "ar"));
    assert(sw_tag(h, "html", sw_no_attrs));
    assert(sw_end(h, "html"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<html lang=\"ar\" dir=\"rtl\">") != NULL);

    sw_buffer_reset(h);
    assert(sw_translator_set_language(translator, "fr"));
    assert(sw_tag(h, "div", sw_attrs(
        sw_attr(sw_translation(0)),
        sw_attr("title", "Search")
    )));
    assert(sw_text(h, "Search"));
    assert(sw_tag(h, "span", sw_attrs(
        sw_attr(sw_translation(1)),
        sw_attr("title", "Search")
    )));
    assert(sw_text(h, "Search"));
    assert(sw_end(h, "span"));
    assert(sw_text(h, "Search"));
    assert(sw_end(h, "div"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<div title=\"Search\">Search<span title=\"Rechercher\">Rechercher</span>Search</div>") != NULL);

    sw_buffer_reset(h);
    assert(sw_tag(h, "div", sw_attrs(
        sw_attr("style", "border-color:currentColor"),
        sw_attr(sw_direction(SW_LANGUAGE_DIRECTION_TTB))
    )));
    assert(sw_text(h, "Search"));
    assert(sw_end(h, "div"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<div dir=\"ltr\" data-sw-direction=\"ttb\" style=\"border-color:currentColor;writing-mode:vertical-rl;text-orientation:mixed;\">Rechercher</div>") != NULL);

    sw_buffer_reset(h);
    assert(sw_tag(h, "section", sw_no_attrs));
    assert(sw_end(h, "section"));
    assert(strstr(sw_buffer_data(h), "<!doctype html>") == NULL);

    sw_buffer_reset(h);
    assert(sw_text_no_translate(h, "prefix"));
    assert(sw_tag(h, "html", sw_no_attrs));
    assert(sw_end(h, "html"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<!doctype html>") == NULL);
    assert(strstr(html, "prefix<html lang=\"fr\" dir=\"ltr\"></html>") != NULL);

    sw_buffer_reset(h);
    assert(sw_tag(h, "html", sw_no_attrs));
    assert(sw_end(h, "html"));
    assert(count_occurrences(sw_buffer_data(h), "<!doctype html>") == 1);

    sw_buffer_reset(h);
    assert(sw_tag(h, "html", sw_no_attrs));
    assert(sw_end(h, "html"));
    assert(count_occurrences(sw_buffer_data(h), "<!doctype html>") == 1);

    sw_buffer_free(h);
    sw_translator_destroy(translator);
}

static void test_html_short_macros(void) {
    sw_translator* translator = sw_translator_create(.code = "en", .label = "English", .direction = SW_LANGUAGE_DIRECTION_LTR);
    sw_buffer* h = sw_buffer_new();
    const c8* html;

    load_fixture_catalog(translator);
    assert(sw_translator_set_language(translator, "fr"));
    sw_buffer_set_translator(h, translator);

    assert(sw_title(h, "Search"));
    assert(sw_title_no_translate(h, "Search"));
    sw_section(h, sw_attrs(
        sw_attr("class", "shell"),
        sw_attr("data-mode", "demo"),
        sw_attr("data-label", "Search"),
        sw_attr_bool("hidden", 1),
        sw_attr_bool("selected", 1)
    ), {
        sw_input(h, sw_attrs(
            sw_attr("type", "text"),
            sw_attr("placeholder", "Search"),
            sw_attr("data-role", "search"),
            sw_attr_bool("disabled", 1)
        ));
        sw_text(h, "Search");
        sw_text_no_translate(h, "Search");
    });
    sw_div(h, sw_no_attrs, {
        sw_a(h, sw_attrs(sw_attr_no_translate("href", "/docs")), {
            sw_text(h, "Docs");
        });
        sw_button(h, sw_attrs(sw_attr_no_translate("type", "button")), {
            sw_text(h, "Open");
        });
    });
    sw_el(h, "custom-card", sw_attrs(sw_attr("data-role", "demo")), {
        sw_text(h, "Custom");
    });

    html = sw_buffer_data(h);
    assert(strstr(html, "<title>Rechercher</title>") != NULL);
    assert(strstr(html, "<title>Search</title>") != NULL);
    assert(strstr(html, "data-mode=\"demo\"") != NULL);
    assert(strstr(html, "data-label=\"Search\"") != NULL);
    assert(strstr(html, "placeholder=\"Rechercher\"") != NULL);
    assert(strstr(html, "data-role=\"search\"") != NULL);
    assert(strstr(html, "hidden") != NULL);
    assert(strstr(html, "selected") != NULL);
    assert(strstr(html, "disabled") != NULL);
    assert(strstr(html, "</input>") == NULL);
    assert(strstr(html, "RechercherSearch") != NULL);
    assert(strstr(html, "<a href=\"/docs\">Docs</a>") != NULL);
    assert(strstr(html, "<button type=\"button\">Open</button>") != NULL);
    assert(strstr(html, "<custom-card data-role=\"demo\">Custom</custom-card>") != NULL);

    sw_buffer_free(h);
    sw_translator_destroy(translator);
}

static void test_js_short_api(void) {
    sw_buffer* h = sw_buffer_new();
    const sw_js_live_opts live_search = {
        .form_id = "search-form",
        .input_id = "search-input",
        .target_id = "search-preview",
        .endpoint = "/preview?label=quo\"te'\n</script>\\\\",
        .value_param = "q",
        .loading_class = "is-loading",
        .debounce_ms = 150,
        .method = SW_JS_POST,
        .swap_mode = SW_JS_INNER,
        .serialize_form = 1,
        .abort_stale = 1,
        .prevent_submit = 1
    };
    const sw_js_fetch_opts fetch_replace = {
        .trigger_id = "refresh-button",
        .form_id = "search-form",
        .target_id = "results",
        .endpoint = "/replace",
        .loading_class = "loading-state",
        .event_type = SW_JS_CLICK,
        .method = SW_JS_GET,
        .swap_mode = SW_JS_OUTER,
        .serialize_form = 1,
        .abort_stale = 1,
        .prevent_default = 1
    };
    const sw_js_toggle_opts toggle = {
        .trigger_id = "toggle-trigger",
        .target_id = "toggle-target",
        .event_type = SW_JS_CHANGE,
        .prevent_default = 0,
        .sync_initial_state = 1,
        .use_trigger_checked = 1,
        .invert = 0
    };
    const sw_js_class_opts class_toggle = {
        .trigger_id = "class-trigger",
        .target_id = "class-target",
        .class_name = "is-active",
        .event_type = SW_JS_CLICK,
        .prevent_default = 1,
        .sync_initial_state = 0,
        .use_trigger_checked = 0,
        .invert = 1
    };
    const c8* html;

    assert(h != NULL);
    assert(sw_js_runtime(h));
    assert(sw_js_runtime(h));
    assert(sw_js_live_cfg(h, &live_search));
    assert(sw_js_fetch_cfg(h, &fetch_replace));
    assert(sw_js_toggle_cfg(h, &toggle));
    assert(sw_js_class_cfg(h, &class_toggle));
    assert(sw_js_live(h,
        .form_id = "macro-form",
        .input_id = "macro-input",
        .target_id = "macro-target",
        .endpoint = "/macro"
    ));
    assert(sw_js_fetch(h,
        .trigger_id = "macro-trigger",
        .target_id = "macro-results",
        .endpoint = "/macro-fetch"
    ));
    assert(sw_js_live_search(h, "search-form", "search-input", "search-preview", "/search-preview"));

    html = sw_buffer_data(h);
    assert(count_occurrences(html, "data-swjs=\"runtime\"") == 1);
    assert(count_occurrences(html, "data-swjs=\"live-search\"") == 3);
    assert(count_occurrences(html, "data-swjs=\"fetch-replace\"") == 2);
    assert(count_occurrences(html, "data-swjs=\"toggle\"") == 1);
    assert(count_occurrences(html, "data-swjs=\"class-toggle\"") == 1);
    assert(strstr(html, "window.__swjsRuntime.liveSearch(") != NULL);
    assert(strstr(html, "\"debounceMs\":150") != NULL);
    assert(strstr(html, "\"debounceMs\":120") != NULL);
    assert(strstr(html, "\"serializeForm\":true") != NULL);
    assert(strstr(html, "\"abortStale\":true") != NULL);
    assert(strstr(html, "\"preventSubmit\":true") != NULL);
    assert(strstr(html, "\"method\":0") != NULL);
    assert(strstr(html, "\"swapMode\":1") != NULL);
    assert(strstr(html, "\"eventType\":2") != NULL);
    assert(strstr(html, "\"className\":\"is-active\"") != NULL);
    assert(strstr(html, "\\\"") != NULL);
    assert(strstr(html, "\\x27") != NULL);
    assert(strstr(html, "\\n") != NULL);
    assert(strstr(html, "\\\\") != NULL);
    assert(strstr(html, "\\x3C/script>") != NULL);

    sw_buffer_reset(h);
    assert(sw_js_runtime(h));
    assert(count_occurrences(sw_buffer_data(h), "data-swjs=\"runtime\"") == 1);
    sw_buffer_free(h);
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
        .uri = "/upload?q=Jane+Doe&empty=&encoded=A%2FB",
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

    assert(sw_http_is(&message, "POST", "/upload"));
    assert(!sw_http_is(&message, "GET", "/upload"));
    assert(!sw_http_is(&message, "POST", "/other"));
    assert(sw_http_get_query(&message, "q", value, sizeof(value)) > 0);
    assert(strcmp(value, "Jane Doe") == 0);
    assert(sw_http_get_query(&message, "encoded", value, sizeof(value)) > 0);
    assert(strcmp(value, "A/B") == 0);
    assert(sw_http_get_query(&message, "missing", value, sizeof(value)) == 0);
    assert(strcmp(value, "") == 0);
    assert(sw_http_get_query(&message, "empty", value, sizeof(value)) == 0);
    assert(strcmp(value, "") == 0);

    assert(sw_http_get_form(&message, "name", value, sizeof(value)) > 0);
    assert(strcmp(value, "Jane Doe") == 0);
    assert(sw_http_get_form(&message, "missing", value, sizeof(value)) == 0);
    assert(strcmp(value, "") == 0);
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

static void test_utility_helpers(void) {
    assert(sw_matches_query("Language", "lang", 0));
    assert(sw_matches_query("Language", "Lang", 1));
    assert(!sw_matches_query("Language", "lang", 1));
    assert(sw_matches_query("HTTP Helpers", "", 0));
    assert(!sw_matches_query("HTTP Helpers", "missing", 0));
    assert(!sw_matches_query(NULL, "http", 0));
    assert(!sw_matches_query("HTTP Helpers", NULL, 0));
}

static char* read_repo_file(const char* relative_path) {
    char full_path[1024];
    FILE* file;
    long file_size;
    char* text;

    assert(snprintf(full_path, sizeof(full_path), "%s/%s", SYPHAX_WEB_SOURCE_DIR, relative_path) > 0);
    file = fopen(full_path, "rb");
    assert(file != NULL);
    assert(fseek(file, 0, SEEK_END) == 0);
    file_size = ftell(file);
    assert(file_size >= 0);
    assert(fseek(file, 0, SEEK_SET) == 0);

    text = (char*)calloc((sz)file_size + 1, 1);
    assert(text != NULL);
    assert(fread(text, 1, (sz)file_size, file) == (sz)file_size);
    fclose(file);
    return text;
}

static void assert_short_surface_only(const char* relative_path) {
    char* text = read_repo_file(relative_path);

    assert(strstr(text, "sw" "_html_") == NULL);
    assert(strstr(text, "sw" "_j_") == NULL);
    assert(strstr(text, "sw" "_kv(") == NULL);
    assert(strstr(text, "sw" "_tr(") == NULL);
    assert(strstr(text, "sw" "_bool(") == NULL);
    assert(strstr(text, "sw" "_txt(") == NULL);
    assert(strstr(text, "sw" "_txt_tr(") == NULL);
    assert(strstr(text, "sw" "_title_tr(") == NULL);
    assert(strstr(text, "sw" "_hbuf") == NULL);
    assert(strstr(text, "sw_buffer_set" "_tr(") == NULL);
    assert(strstr(text, "sw_buffer_get" "_tr(") == NULL);
    free(text);
}

static void assert_simple_example_surface(const char* relative_path) {
    char* text = read_repo_file(relative_path);

    assert(strstr(text, "query[0] = '\\0'") == NULL);
    assert(strstr(text, "sw_http_get_var(") == NULL);
    free(text);
}

static void test_public_short_names(void) {
    static const char* const paths[] = {
        "include/sw_html.h",
        "include/sw_js.h",
        "examples/static.c",
        "tests/test_suite.c",
        "README.md"
    };
    static const char* const easy_paths[] = {
        "examples/static.c",
        "README.md"
    };
    sz i;

    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        assert_short_surface_only(paths[i]);
    }
    for (i = 0; i < sizeof(easy_paths) / sizeof(easy_paths[0]); ++i) {
        assert_simple_example_surface(easy_paths[i]);
    }
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
#ifdef _WIN32
    {
        DWORD temp_dir_len = GetTempPathA((DWORD)sizeof(file_path), file_path);
        assert(temp_dir_len > 0);
        assert(temp_dir_len < sizeof(file_path));
        assert(snprintf(file_path + temp_dir_len, sizeof(file_path) - (sz)temp_dir_len, "%s", file_name) > 0);
    }
#else
    assert(snprintf(file_path, sizeof(file_path), "/tmp/%s", file_name) > 0);
#endif
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
    assert(strstr(response.data, "id=\"sw-search-preview\"") != NULL);
    assert(strstr(response.data, "/example.js") == NULL);
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

    response = issue_request(mgr, port, "GET /search-preview HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "Content-Type: text/html; charset=utf-8") != NULL);
    assert(strstr(response.data, "<!doctype html>") == NULL);
    assert(strstr(response.data, "class=\"sw-preview-shell\"") != NULL);
    assert(strstr(response.data, "Type to search.") != NULL);
    free(response.data);

    response = issue_request(mgr, port,
        "GET /search-preview?q=Language HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "<!doctype html>") == NULL);
    assert(strstr(response.data, "<p>Language</p>") != NULL);
    free(response.data);

    response = issue_request(mgr, port,
        "GET /?q=Language HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "<!doctype html>") != NULL);
    assert(strstr(response.data, "id=\"sw-search-preview\"") != NULL);
    assert(strstr(response.data, "<p>Language</p>") != NULL);
    free(response.data);

    response = issue_request(mgr, port, "GET /file HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "static payload") != NULL);
    free(response.data);

    assert(state.request_count == 7);
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
    { "html_short_api", test_html_short_api },
    { "html_short_macros", test_html_short_macros },
    { "js_short_api", test_js_short_api },
    { "public_short_names", test_public_short_names },
    { "request_helpers", test_request_helpers },
    { "utility_helpers", test_utility_helpers },
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
