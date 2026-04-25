#include "sw_html.h"
#include "sw_js.h"
#include "sw_server.h"
#include "sw_translator.h"
#include "sw_utility.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
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
#    include <sys/stat.h>
#    include <unistd.h>
#endif

#if defined(SYPHAX_WEB_HAS_TLS)
#    include <openssl/pem.h>
#    include <openssl/rsa.h>
#    include <openssl/ssl.h>
#    include <openssl/x509v3.h>
#endif

typedef struct {
    const c8* docroot;
    const c8* file_name;
    int request_count;
} sw_test_server_state;

typedef struct {
    int request_count;
    b8 saw_secure;
    char alpn[16];
} sw_test_tls_state;

typedef struct {
    char* data;
    sz size;
    b8 closed;
} sw_test_response;

#if defined(SYPHAX_WEB_HAS_TLS)
static void write_test_tls_files(const char* cert_path, const char* key_path) {
    EVP_PKEY* pkey = NULL;
    EVP_PKEY_CTX* pkey_ctx = NULL;
    X509* cert = NULL;
    X509_NAME* name = NULL;
    X509_EXTENSION* extension = NULL;
    FILE* cert_file = NULL;
    FILE* key_file = NULL;

    pkey_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, NULL);
    assert(pkey_ctx != NULL);
    assert(EVP_PKEY_keygen_init(pkey_ctx) > 0);
    assert(EVP_PKEY_CTX_set_rsa_keygen_bits(pkey_ctx, 2048) > 0);
    assert(EVP_PKEY_keygen(pkey_ctx, &pkey) > 0);
    EVP_PKEY_CTX_free(pkey_ctx);

    cert = X509_new();
    assert(cert != NULL);
    assert(ASN1_INTEGER_set(X509_get_serialNumber(cert), 1) == 1);
    assert(X509_gmtime_adj(X509_get_notBefore(cert), 0) != NULL);
    assert(X509_gmtime_adj(X509_get_notAfter(cert), 3600) != NULL);
    assert(X509_set_pubkey(cert, pkey) == 1);

    name = X509_get_subject_name(cert);
    assert(name != NULL);
    assert(X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC, (const unsigned char*)"localhost", -1, -1, 0) == 1);
    assert(X509_set_issuer_name(cert, name) == 1);

    extension = X509V3_EXT_conf_nid(NULL, NULL, NID_subject_alt_name, "DNS:localhost,IP:127.0.0.1");
    assert(extension != NULL);
    assert(X509_add_ext(cert, extension, -1) == 1);
    X509_EXTENSION_free(extension);

    assert(X509_sign(cert, pkey, EVP_sha256()) > 0);

    key_file = fopen(key_path, "wb");
    assert(key_file != NULL);
    assert(PEM_write_PrivateKey(key_file, pkey, NULL, NULL, 0, NULL, NULL) == 1);
    fclose(key_file);

    cert_file = fopen(cert_path, "wb");
    assert(cert_file != NULL);
    assert(PEM_write_X509(cert_file, cert) == 1);
    fclose(cert_file);

    X509_free(cert);
    EVP_PKEY_free(pkey);
}
#endif

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

static void translation_file_path(char* buffer, sz buffer_len) {
    repo_path(buffer, buffer_len, "resources/translations.json");
}

static void temp_path(char* buffer, sz buffer_len, const char* name) {
    assert(buffer != NULL);
    assert(buffer_len > 0);
    assert(name != NULL);
#ifdef _WIN32
    {
        DWORD temp_dir_len = GetTempPathA((DWORD)buffer_len, buffer);
        assert(temp_dir_len > 0);
        assert(temp_dir_len < buffer_len);
        assert(snprintf(buffer + temp_dir_len, buffer_len - (sz)temp_dir_len, "%s", name) > 0);
    }
#else
    assert(snprintf(buffer, buffer_len, "/tmp/%s", name) > 0);
#endif
}

static void unique_name(const char* seed_name, char* buffer, sz buffer_len) {
    static unsigned long counter = 0;
    const char* ext;
#ifdef _WIN32
    const unsigned long pid = (unsigned long)GetCurrentProcessId();
#else
    const unsigned long pid = (unsigned long)getpid();
#endif

    assert(buffer != NULL);
    assert(buffer_len > 0);

    ext = (seed_name != NULL) ? strrchr(seed_name, '.') : NULL;
    if (ext == NULL) {
        ext = "";
    }

    ++counter;
    assert(snprintf(buffer, buffer_len, "syphax_web_%lu_%lu%s", pid, counter, ext) > 0);
}

static void sw_test_sleep_ms(i32 duration_ms) {
#ifdef _WIN32
    Sleep((DWORD)(duration_ms > 0 ? duration_ms : 0));
#else
    usleep((useconds_t)((duration_ms > 0 ? duration_ms : 0) * 1000));
#endif
}

static void create_temp_directory(char* buffer, sz buffer_len, const char* seed_name) {
    char candidate_name[256];
    int attempt;

    assert(buffer != NULL);
    assert(buffer_len > 0);
    assert(seed_name != NULL);

    for (attempt = 0; attempt < 16; ++attempt) {
        unique_name(seed_name, candidate_name, sizeof(candidate_name));
        temp_path(buffer, buffer_len, candidate_name);
#ifdef _WIN32
        if (CreateDirectoryA(buffer, NULL) != 0) {
            return;
        }
#else
        if (mkdir(buffer, 0700) == 0) {
            return;
        }
        if (errno != EEXIST) {
            break;
        }
#endif
    }

    assert(!"failed to create temporary directory");
}

static void add_fixture_languages(sw_translator* translator) {
    sw_translator_add(translator, .code = "ar", .label = "Arabic", .direction = SW_LANGUAGE_DIRECTION_RTL);
    sw_translator_add(translator, .code = "fa", .label = "Farsi", .direction = SW_LANGUAGE_DIRECTION_RTL);
    sw_translator_add(translator, .code = "zh", .label = "Chinese", .direction = SW_LANGUAGE_DIRECTION_LTR);
    sw_translator_add(translator, .code = "ja", .label = "Japanese", .direction = SW_LANGUAGE_DIRECTION_LTR);
}

static void load_fixture_translations(sw_translator* translator) {
    char path[1024];

    add_fixture_languages(translator);
    translation_file_path(path, sizeof(path));
    assert(sw_translator_load_all_json_file(translator, path));
}

static void load_fixture_french(sw_translator* translator) {
    sw_translator_add(translator, .code = "fr", .label = "French", .direction = SW_LANGUAGE_DIRECTION_LTR);
    assert(sw_translator_load_json_text(translator, "fr",
        "{"
            "\"Search\":\"Rechercher\","
            "\"Language\":\"Langue\""
        "}"));
}

static sw_translator* create_fixture_translator(void) {
    char path[1024];
    sw_translator* translator;

    translation_file_path(path, sizeof(path));
    translator = sw_translator_create(path, .code = "en", .label = "English", .direction = SW_LANGUAGE_DIRECTION_LTR);
    assert(translator != NULL);
    add_fixture_languages(translator);
    return translator;
}

static void sw_test_handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    sw_test_server_state* state = (sw_test_server_state*)user_data;
    char query[128];

    state->request_count += 1;

    if (sw_http_is(request, "GET", "/")) {
        sw_buffer* h = sw_buffer_new();
        assert(sw_http_get_query(request, "q", query, sizeof(query)) >= 0);
        sw_html(h, sw_attrs(), {
            sw_body(h, sw_attrs(), {
                sw_h1(h, sw_attrs(), {
                    sw_text(h, "Syphax Web");
                });
                sw_div(h, sw_attrs(sw_attr("id", "sw-search-preview")), {
                    sw_section(h, sw_attrs(sw_attr("class", "sw-preview-shell")), {
                        sw_p(h, sw_attrs(), {
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
        assert(sw_http_serve_path(connection, state->docroot, state->file_name) == 0);
        return;
    }

    if (request->method != NULL && strcmp(request->method, "GET") == 0
        && request->uri != NULL && strncmp(request->uri, "/public/", 8) == 0) {
        assert(sw_http_serve_path(connection, state->docroot, request->uri + 7) == 0);
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
            sw_p(h, sw_attrs(), {
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

static void sw_tls_test_handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    sw_test_tls_state* state = (sw_test_tls_state*)user_data;
    const c8* alpn = sw_connection_alpn(connection);

    state->request_count += 1;
    state->saw_secure = sw_connection_is_secure(connection);
    snprintf(state->alpn, sizeof(state->alpn), "%s", alpn != NULL ? alpn : "");

    if (sw_http_is(request, "GET", "/")) {
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8",
            "secure=%d alpn=%s proto=%s",
            state->saw_secure ? 1 : 0,
            state->alpn,
            request->proto != NULL ? request->proto : "");
        return;
    }

    sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "Not Found");
}

static void test_translator(void) {
    sw_translator* translator = sw_translator_create(NULL, .code = "en", .label = "English", .direction = SW_LANGUAGE_DIRECTION_LTR);
    sw_translator* auto_loaded;
    char translations_path[1024];
    const sw_language* languages;
    const sw_language* current_language;
    sz language_count = 0;

    assert(translator != NULL);
    current_language = sw_translator_get_language(translator);
    assert(current_language != NULL);
    assert(strcmp(current_language->code, "en") == 0);
    assert(!sw_translator_set_language(translator, "ja"));
    assert(!sw_translator_add(translator, .code = NULL, .label = "Broken", .direction = SW_LANGUAGE_DIRECTION_LTR));
    assert(!sw_translator_add(translator, .code = "", .label = "Broken", .direction = SW_LANGUAGE_DIRECTION_LTR));

    sw_translator_add(translator, .code = "de", .label = "German", .direction = SW_LANGUAGE_DIRECTION_LTR);
    assert(sw_translator_set_language(translator, "de"));
    current_language = sw_translator_get_language(translator);
    assert(current_language != NULL);
    assert(strcmp(current_language->code, "de") == 0);
    assert(strcmp(sw_translate(translator, "Search"), "Search") == 0);
    assert(sw_translator_set_language(translator, "en"));

    load_fixture_translations(translator);
    assert(sw_translator_set_language(translator, "en"));
    assert(strcmp(sw_translate(translator, "Search"), "Search") == 0);
    assert(sw_translator_set_language(translator, "ja"));
    assert(strcmp(sw_translate(translator, "Search"), "検索") == 0);
    assert(sw_translator_set_language(translator, "ar"));
    assert(strcmp(sw_translate(translator, "Language"), "اللغة") == 0);
    assert(sw_translator_set_language(translator, "fa"));
    assert(strcmp(sw_translate(translator, "Language"), "زبان") == 0);
    assert(sw_translator_set_language(translator, "zh"));
    assert(strcmp(sw_translate(translator, "Language"), "语言") == 0);
    assert(strcmp(sw_translate(translator, "Unknown"), "Unknown") == 0);

    assert(!sw_translator_load_all_json_text(translator, "[\"bad\"]"));
    assert(!sw_translator_load_all_json_text(translator, "{\"ar\": 1}"));
    assert(!sw_translator_load_all_json_text(translator, "{\"ar\": {\"Search\": 1}}"));
    assert(!sw_translator_load_all_json_text(translator, "{\"Search\": {\"ar\": \"A\", \"ar\": \"B\"}}"));
    assert(!sw_translator_load_all_json_text(translator, "{\"Search\": {\"Language\": \"A\"}}"));
    assert(!sw_translator_load_json_file(translator, "fr", "resources/missing-translations.json"));

    assert(sw_translator_load_all_json_text(translator,
        "{"
            "\"Search\":{\"fr\":\"Chercher\"},"
            "\"Language\":{\"fr\":\"Langage\"}"
        "}"));
    assert(sw_translator_set_language(translator, "fr"));
    assert(strcmp(sw_translate(translator, "Search"), "Chercher") == 0);
    assert(strcmp(sw_translate(translator, "Language"), "Langage") == 0);

    load_fixture_french(translator);
    assert(sw_translator_set_language(translator, "fr"));
    assert(strcmp(sw_translate(translator, "Search"), "Rechercher") == 0);
    assert(strcmp(sw_translate(translator, "Language"), "Langue") == 0);
    assert(strcmp(sw_translate(translator, "Home"), "Home") == 0);

    assert(!sw_translator_load_json_text(translator, "fr", "[\"bad\"]"));
    assert(!sw_translator_load_json_text(translator, "fr", "{\"Search\": 1}"));

    assert(sw_translator_load_json_text(translator, "fr",
        "{"
            "\"Search\":\"Chercher\","
            "\"Language\":\"Langage\""
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

    translation_file_path(translations_path, sizeof(translations_path));
    auto_loaded = sw_translator_create(translations_path, .code = "en", .label = "English", .direction = SW_LANGUAGE_DIRECTION_LTR);
    assert(auto_loaded != NULL);
    assert(sw_translator_set_language(auto_loaded, "ja"));
    assert(strcmp(sw_translate(auto_loaded, "Search"), "検索") == 0);
    languages = sw_translator_get_languages(auto_loaded, &language_count);
    assert(languages != NULL);
    assert(language_count == 5);
    assert(strcmp(languages[0].code, "en") == 0);
    assert(strcmp(languages[1].code, "ar") == 0);
    assert(strcmp(languages[2].code, "fa") == 0);
    assert(strcmp(languages[3].code, "zh") == 0);
    assert(strcmp(languages[4].code, "ja") == 0);
    sw_translator_destroy(auto_loaded);

    sw_translator_destroy(translator);
}

static void test_html_short_api(void) {
    sw_translator* translator = create_fixture_translator();
    sw_buffer* h = sw_buffer_new();
    const c8* html;

    assert(sw_translator_set_language(translator, "zh"));
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

    assert(strstr(sw_buffer_data(h), "title=\"搜索\"") != NULL);
    assert(strstr(sw_buffer_data(h), "placeholder=\"搜索\"") != NULL);
    assert(strstr(sw_buffer_data(h), "aria-label=\"搜索\"") != NULL);
    assert(strstr(sw_buffer_data(h), "alt=\"Search\"") != NULL);
    assert(strstr(sw_buffer_data(h), "value=\"&quot;quoted&quot;\"") != NULL);
    assert(strstr(sw_buffer_data(h), "搜索Search") != NULL);
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
    assert(strstr(html, ">Search搜索</div>") != NULL);

    sw_buffer_reset(h);
    assert(sw_tag(h, "HTML", sw_attrs()));
    assert(sw_end(h, "HTML"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<!doctype html><HTML") != NULL);
    assert(count_occurrences(html, "<!doctype html>") == 1);

    sw_buffer_reset(h);
    assert(sw_translator_set_language(translator, "ar"));
    assert(sw_tag(h, "html", sw_attrs()));
    assert(sw_end(h, "html"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<html lang=\"ar\" dir=\"rtl\">") != NULL);

    sw_buffer_reset(h);
    assert(sw_translator_set_language(translator, "zh"));
    assert(sw_tag(h, "div", sw_attrs(
        sw_attr_translation(0),
        sw_attr("title", "Search")
    )));
    assert(sw_text(h, "Search"));
    assert(sw_tag(h, "span", sw_attrs(
        sw_attr_translation(1),
        sw_attr("title", "Search")
    )));
    assert(sw_text(h, "Search"));
    assert(sw_end(h, "span"));
    assert(sw_text(h, "Search"));
    assert(sw_end(h, "div"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<div title=\"Search\">Search<span title=\"搜索\">搜索</span>Search</div>") != NULL);

    sw_buffer_reset(h);
    assert(sw_tag(h, "div", sw_attrs(
        sw_attr("style", "border-color:currentColor"),
        sw_attr_direction(SW_LANGUAGE_DIRECTION_TTB)
    )));
    assert(sw_text(h, "Search"));
    assert(sw_end(h, "div"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<div dir=\"ltr\" data-sw-direction=\"ttb\" style=\"border-color:currentColor;writing-mode:vertical-rl;text-orientation:mixed;\">搜索</div>") != NULL);

    sw_buffer_reset(h);
    assert(sw_tag(h, "section", sw_attrs()));
    assert(sw_end(h, "section"));
    assert(strstr(sw_buffer_data(h), "<!doctype html>") == NULL);

    sw_buffer_reset(h);
    assert(sw_text_no_translate(h, "prefix"));
    assert(sw_tag(h, "html", sw_attrs()));
    assert(sw_end(h, "html"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<!doctype html>") == NULL);
    assert(strstr(html, "prefix<html lang=\"zh\" dir=\"ltr\"></html>") != NULL);

    sw_buffer_reset(h);
    assert(sw_tag(h, "html", sw_attrs()));
    assert(sw_end(h, "html"));
    assert(count_occurrences(sw_buffer_data(h), "<!doctype html>") == 1);

    sw_buffer_reset(h);
    assert(sw_tag(h, "html", sw_attrs()));
    assert(sw_end(h, "html"));
    assert(count_occurrences(sw_buffer_data(h), "<!doctype html>") == 1);

    sw_buffer_free(h);
    sw_translator_destroy(translator);
}

static void test_html_short_macros(void) {
    sw_translator* translator = create_fixture_translator();
    sw_buffer* h = sw_buffer_new();
    const c8* html;

    assert(sw_translator_set_language(translator, "zh"));
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
    sw_div(h, sw_attrs(), {
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
    assert(strstr(html, "<title>搜索</title>") != NULL);
    assert(strstr(html, "<title>Search</title>") != NULL);
    assert(strstr(html, "data-mode=\"demo\"") != NULL);
    assert(strstr(html, "data-label=\"Search\"") != NULL);
    assert(strstr(html, "placeholder=\"搜索\"") != NULL);
    assert(strstr(html, "data-role=\"search\"") != NULL);
    assert(strstr(html, "hidden") != NULL);
    assert(strstr(html, "selected") != NULL);
    assert(strstr(html, "disabled") != NULL);
    assert(strstr(html, "</input>") == NULL);
    assert(strstr(html, "搜索Search") != NULL);
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
    assert((sw_js_live)(h, &live_search));
    assert((sw_js_fetch)(h, &fetch_replace));
    assert((sw_js_toggle)(h, &toggle));
    assert((sw_js_class)(h, &class_toggle));
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
    assert(strstr(html, "data-sw-state") != NULL);
    assert(strstr(html, "aria-busy") != NULL);
    assert(strstr(html, "restoreFocus") != NULL);
    assert(strstr(html, "data-sw-error") != NULL);

    sw_buffer_reset(h);
    assert(sw_js_runtime(h));
    assert(count_occurrences(sw_buffer_data(h), "data-swjs=\"runtime\"") == 1);
    sw_buffer_free(h);
}

static void test_request_helpers(void) {
    sw_http_header headers[] = {
        { "Content-Type", "multipart/form-data; boundary=\"demo\"" }
    };
    const c8 multipart_body[] =
        "--demo\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"hello.txt\"\r\n"
        "Content-Type: text/plain\r\n"
        "\r\n"
        "ab\0cd\r\n"
        "--demo\r\n"
        "Content-Disposition: form-data; name=\"note\"\r\n"
        "\r\n"
        "hello\r\n"
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
        .body = multipart_body,
        .body_len = sizeof(multipart_body) - 1,
        .content_length = sizeof(multipart_body) - 1
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
    memset(&part, 0, sizeof(part));
    assert(sw_http_next_multipart(&multipart_message, &part, &offset) == 1);
    assert(strcmp(part.name, "file") == 0);
    assert(strcmp(part.filename, "hello.txt") == 0);
    assert(strcmp(part.content_type, "text/plain") == 0);
    assert(part.data_len == 5);
    assert(memcmp(part.data, "ab\0cd", part.data_len) == 0);
    sw_http_multipart_clear(&part);

    memset(&part, 0, sizeof(part));
    assert(sw_http_next_multipart(&multipart_message, &part, &offset) == 1);
    assert(strcmp(part.name, "note") == 0);
    assert(part.filename == NULL);
    assert(part.content_type == NULL);
    assert(part.data_len == 5);
    assert(memcmp(part.data, "hello", part.data_len) == 0);
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
    assert(strstr(text, "sw_http_get" "_var(") == NULL);
    assert(strstr(text, "sw_http_serve" "_file(") == NULL);
    assert(strstr(text, "sw_https" "_listen(") == NULL);
    assert(strstr(text, "sw_add" "_language(") == NULL);
    assert(strstr(text, "sw_js_live" "_c" "fg(") == NULL);
    assert(strstr(text, "sw_js_fetch" "_c" "fg(") == NULL);
    assert(strstr(text, "sw_js_toggle" "_c" "fg(") == NULL);
    assert(strstr(text, "sw_js_class" "_c" "fg(") == NULL);
    assert(strstr(text, "sw_translator_create" "_internal") == NULL);
    assert(strstr(text, "sw_add_language" "_internal") == NULL);
    assert(strstr(text, "sw_translation" "(") == NULL);
    assert(strstr(text, "sw_direction" "(") == NULL);
    assert(strstr(text, "sw_get" "_time(") == NULL);
    assert(strstr(text, "sw_get_file" "_content(") == NULL);
    assert(strstr(text, "sw_generate_unique" "_filename(") == NULL);
    assert(strstr(text, "sw_random" "(") == NULL);
    assert(strstr(text, "sw_hash" "(") == NULL);
    assert(strstr(text, "03_" "complex") == NULL);
    assert(strstr(text, "04_" "complex") == NULL);
    free(text);
}

static void test_public_short_names(void) {
    static const char* const paths[] = {
        "include/sw_html.h",
        "include/sw_js.h",
        "include/sw_server.h",
        "include/sw_translator.h",
        "include/sw_utility.h",
        "examples/example_common.h",
        "examples/01_http.c",
        "examples/02_https.c",
        "examples/03_static_site.c",
        "examples/04_live_queue.c",
        "examples/05_folder_app.c",
        "tests/test_suite.c",
        "README.md"
    };
    static const char* const easy_paths[] = {
        "examples/01_http.c",
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

static sw_test_response issue_request_bytes(sw_mgr* mgr, u16 port, const void* request, sz request_len) {
    sw_test_socket fd = connect_to_port(port);
    sw_test_response response = {0};
    sz response_len = 0;
    int attempts;

    response.data = (char*)calloc(1, 65536);
    assert(response.data != NULL);
    assert(send(fd, request, (int)request_len, 0) == (int)request_len);

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

static sw_test_response read_response_from_socket(sw_mgr* mgr, sw_test_socket fd, int max_attempts, i32 poll_ms, i32 sleep_ms) {
    sw_test_response response = {0};
    sz response_len = 0;
    int attempts;

    response.data = (char*)calloc(1, 65536);
    assert(response.data != NULL);

    for (attempts = 0; attempts < max_attempts; ++attempts) {
        char chunk[4096];
        int received;
        if (sleep_ms > 0) {
            sw_test_sleep_ms(sleep_ms);
        }
        assert(sw_mgr_poll(mgr, poll_ms) >= 0);
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

#if defined(SYPHAX_WEB_HAS_TLS)
static b8 sw_test_ssl_wants_retry(SSL* ssl, int rc) {
    const int err = SSL_get_error(ssl, rc);
    return err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE;
}

static b8 sw_test_ssl_error_wants_retry(int err) {
    return err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE;
}

static void sw_test_tls_handshake(sw_mgr* mgr, SSL* ssl) {
    int attempts;

    for (attempts = 0; attempts < 200; ++attempts) {
        const int rc = SSL_connect(ssl);
        if (rc == 1) {
            return;
        }
        assert(sw_test_ssl_wants_retry(ssl, rc));
        assert(sw_mgr_poll(mgr, 10) >= 0);
    }

    assert(!"TLS client handshake timed out");
}

static void sw_test_tls_write_all(sw_mgr* mgr, SSL* ssl, const c8* data, sz data_len) {
    sz offset = 0;
    int attempts = 0;

    while (offset < data_len && attempts < 200) {
        const sz remaining = data_len - offset;
        const int chunk_len = remaining > (sz)INT_MAX ? INT_MAX : (int)remaining;
        const int rc = SSL_write(ssl, data + offset, chunk_len);
        if (rc > 0) {
            offset += (sz)rc;
            attempts = 0;
            continue;
        }
        assert(sw_test_ssl_wants_retry(ssl, rc));
        assert(sw_mgr_poll(mgr, 10) >= 0);
        ++attempts;
    }

    assert(offset == data_len);
}

static b8 sw_test_response_has_complete_body(const sw_test_response* response) {
    return response != NULL
        && strstr(response->data, "\r\n\r\n") != NULL
        && strstr(response->data, "Content-Length:") != NULL
        && response_content_length(response) == response_body_size(response);
}

static sw_test_response issue_tls_request(
    sw_mgr* mgr,
    u16 port,
    const c8* request,
    const unsigned char* alpn,
    unsigned int alpn_len,
    char* selected_alpn,
    sz selected_alpn_len
) {
    SSL_CTX* ctx;
    SSL* ssl;
    sw_test_socket fd;
    sw_test_response response = {0};
    sz response_len = 0;
    int attempts;

    if (selected_alpn != NULL && selected_alpn_len > 0) {
        selected_alpn[0] = '\0';
    }

    assert(OPENSSL_init_ssl(0, NULL) == 1);
    ctx = SSL_CTX_new(TLS_client_method());
    assert(ctx != NULL);
    SSL_CTX_set_verify(ctx, SSL_VERIFY_NONE, NULL);

    fd = connect_to_port(port);
    ssl = SSL_new(ctx);
    assert(ssl != NULL);
    SSL_set_fd(ssl, (int)fd);
    SSL_set_connect_state(ssl);
    assert(SSL_set_tlsext_host_name(ssl, "localhost") == 1);
    if (alpn != NULL && alpn_len > 0) {
        assert(SSL_set_alpn_protos(ssl, alpn, alpn_len) == 0);
    }

    sw_test_tls_handshake(mgr, ssl);

    if (selected_alpn != NULL && selected_alpn_len > 0) {
        const unsigned char* selected = NULL;
        unsigned int selected_len = 0;
        sz copy_len;
        SSL_get0_alpn_selected(ssl, &selected, &selected_len);
        copy_len = selected_len < selected_alpn_len - 1 ? selected_len : selected_alpn_len - 1;
        if (selected != NULL && copy_len > 0) {
            memcpy(selected_alpn, selected, copy_len);
        }
        selected_alpn[copy_len] = '\0';
    }

    sw_test_tls_write_all(mgr, ssl, request, strlen(request));

    response.data = (char*)calloc(1, 65536);
    assert(response.data != NULL);
    for (attempts = 0; attempts < 200; ++attempts) {
        char chunk[4096];
        const int rc = SSL_read(ssl, chunk, sizeof(chunk));
        if (rc > 0) {
            memcpy(response.data + response_len, chunk, (sz)rc);
            response_len += (sz)rc;
            response.data[response_len] = '\0';
            response.size = response_len;
            if (sw_test_response_has_complete_body(&response)) {
                break;
            }
            continue;
        }
        {
            const int err = SSL_get_error(ssl, rc);
            if (err == SSL_ERROR_ZERO_RETURN) {
                response.closed = 1;
                break;
            }
            assert(sw_test_ssl_error_wants_retry(err));
        }
        assert(sw_mgr_poll(mgr, 10) >= 0);
    }

    response.size = response_len;
    (void)SSL_shutdown(ssl);
    SSL_free(ssl);
    SSL_CTX_free(ctx);
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    return response;
}
#endif

static void test_live_server(void) {
    sw_mgr* mgr = sw_mgr_create(NULL);
    sw_test_server_state state;
    char file_name[256];
    char docroot_path[512];
    char file_path[512];
    char request_buffer[1024];
    FILE* file;
    u16 port;
    sw_test_response response;
    int listen_rc;

    assert(mgr != NULL);

    create_temp_directory(docroot_path, sizeof(docroot_path), "assets");

    unique_name("fixture.txt", file_name, sizeof(file_name));
    assert(snprintf(file_path, sizeof(file_path), "%s/%s", docroot_path, file_name) > 0);
    file = fopen(file_path, "wb");
    assert(file != NULL);
    fputs("public payload", file);
    fclose(file);

    state.docroot = docroot_path;
    state.file_name = file_name;
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
    assert(strstr(response.data, "public payload") != NULL);
    free(response.data);

    assert(snprintf(request_buffer, sizeof(request_buffer),
        "GET /public/%s HTTP/1.1\r\nHost: localhost\r\n\r\n", file_name) > 0);
    response = issue_request_bytes(mgr, port, request_buffer, strlen(request_buffer));
    assert_complete_response(&response);
    assert(strstr(response.data, "public payload") != NULL);
    free(response.data);

    response = issue_request(mgr, port, "GET /public/%2e%2e/fixture.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 403 Forbidden") != NULL);
    free(response.data);

    response = issue_request(mgr, port, "GET /public/missing.txt HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 404 Not Found") != NULL);
    free(response.data);

    assert(state.request_count == 10);
    remove(file_path);
#ifdef _WIN32
    RemoveDirectoryA(docroot_path);
#else
    rmdir(docroot_path);
#endif
    sw_mgr_destroy(mgr);
}

static void test_server_config(void) {
    sw_http_config config = sw_http_config_default();
    sw_mgr* mgr;
    sw_test_server_state state = {0};
    u16 port;
    sw_test_response response;
    sw_test_socket fd;

    config.max_header_bytes = 96;
    config.max_body_bytes = 16;
    config.max_header_count = 2;
    config.header_timeout_ms = 30;
    config.body_timeout_ms = 30;
    config.idle_timeout_ms = 30;

    mgr = sw_mgr_create(&config);
    assert(mgr != NULL);
    assert(sw_http_listen(mgr, "http://127.0.0.1:0", sw_test_handler, &state) == 0);
    port = sw_mgr_get_listener_port(mgr, 0);
    assert(port != 0);

    response = issue_request(mgr, port,
        "POST /form HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 18446744073709551615\r\n"
        "\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 413 Payload Too Large") != NULL);
    free(response.data);

    response = issue_request(mgr, port,
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "X-One: 1\r\n"
        "X-Two: 2\r\n"
        "\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 431 Request Header Fields Too Large") != NULL);
    free(response.data);

    response = issue_request(mgr, port,
        "POST /form HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 20\r\n"
        "\r\n"
        "12345678901234567890");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 413 Payload Too Large") != NULL);
    free(response.data);

    fd = connect_to_port(port);
    assert(send(fd, "GET / HTTP/1.1\r\n", (int)strlen("GET / HTTP/1.1\r\n"), 0) == (int)strlen("GET / HTTP/1.1\r\n"));
    response = read_response_from_socket(mgr, fd, 40, 5, 5);
    assert(strstr(response.data, "HTTP/1.1 408 Request Timeout") != NULL);
    free(response.data);
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif

    fd = connect_to_port(port);
    assert(send(fd,
        "POST /form HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Length: 10\r\n"
        "\r\n"
        "12",
        (int)strlen(
            "POST /form HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 10\r\n"
            "\r\n"
            "12"),
        0) == (int)strlen(
            "POST /form HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Length: 10\r\n"
            "\r\n"
            "12"));
    response = read_response_from_socket(mgr, fd, 40, 5, 5);
    assert(strstr(response.data, "HTTP/1.1 408 Request Timeout") != NULL);
    free(response.data);
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif

    sw_mgr_destroy(mgr);
}

static void test_tls_support(void) {
    sw_tls_config tls = sw_tls_config_default();
    sw_mgr* mgr = sw_mgr_create(NULL);

    assert(mgr != NULL);

#if defined(SYPHAX_WEB_HAS_TLS)
    {
        static const unsigned char alpn_h2_http11[] = { 2, 'h', '2', 8, 'h', 't', 't', 'p', '/', '1', '.', '1' };
        sw_test_tls_state state = {0};
        char cert_name[256];
        char key_name[256];
        char cert_path[512];
        char key_path[512];
        char selected_alpn[16];
        u16 port;
        sw_test_response response;

        unique_name("syphax-web-test-cert.pem", cert_name, sizeof(cert_name));
        unique_name("syphax-web-test-key.pem", key_name, sizeof(key_name));
        temp_path(cert_path, sizeof(cert_path), cert_name);
        temp_path(key_path, sizeof(key_path), key_name);
        write_test_tls_files(cert_path, key_path);

        tls.certificate_file = cert_path;
        tls.private_key_file = key_path;
        tls.handshake_timeout_ms = 250;

        assert(sw_http_listen_tls(mgr, "http://127.0.0.1:0", &tls, sw_tls_test_handler, &state) != 0);
        assert(sw_http_listen_tls(mgr, "https://127.0.0.1:0", &tls, sw_tls_test_handler, &state) == 0);
        port = sw_mgr_get_listener_port(mgr, 0);
        assert(port != 0);

        response = issue_tls_request(mgr, port,
            "GET / HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n",
            alpn_h2_http11,
            sizeof(alpn_h2_http11),
            selected_alpn,
            sizeof(selected_alpn));
        assert(sw_test_response_has_complete_body(&response));
        assert(strstr(response.data, "HTTP/1.1 200 OK") != NULL);
        assert(strstr(response.data, "secure=1") != NULL);
        assert(strstr(response.data, "alpn=http/1.1") != NULL);
        assert(strstr(response.data, "proto=HTTP/1.1") != NULL);
        assert(strcmp(selected_alpn, "http/1.1") == 0);
        assert(state.request_count == 1);
        assert(state.saw_secure);
        assert(strcmp(state.alpn, "http/1.1") == 0);
        free(response.data);

        remove(cert_path);
        remove(key_path);
    }
#else
    tls.certificate_file = "missing-cert.pem";
    tls.private_key_file = "missing-key.pem";
    assert(sw_http_listen_tls(mgr, "https://127.0.0.1:0", &tls, sw_tls_test_handler, NULL) != 0);
#endif

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
    { "live_server", test_live_server },
    { "server_config", test_server_config },
    { "tls_support", test_tls_support }
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
