#include "sw_html.h"
#include "sw_js.h"
#include "sw_db.h"
#include "sw_server.h"
#include "sw_translator.h"
#include "sw_utility.h"
#include "syphax/s_thread.h"

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
    const c8* upload_path;
    sw_sessions* sessions;
    sw_tokens* tokens;
    sz uploaded_size;
    b8 upload_body_pending;
    b8 upload_seen_file;
    b8 expect_upload_failure;
    char uploaded_name[128];
    char upload_note[128];
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

enum {
    SW_TEST_TRANSFER_CHUNK_BYTES = 16 * 1024
};

static b8 response_has_complete_body(const sw_test_response* response);

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

static i32 sw_test_upload_path(const sw_http_multipart* part, c8* path, sz path_len, void* user_data) {
    sw_test_server_state* state = (sw_test_server_state*)user_data;
    int name_written;
    int path_written;

    assert(part != NULL);
    assert(part->filename != NULL);
    assert(state != NULL);
    assert(state->upload_path != NULL);

    name_written = snprintf(state->uploaded_name, sizeof(state->uploaded_name), "%s", part->filename);
    path_written = snprintf(path, path_len, "%s", state->upload_path);
    if (name_written < 0 || (sz)name_written >= sizeof(state->uploaded_name)) {
        return -1;
    }
    return path_written >= 0 && (sz)path_written < path_len ? 0 : -1;
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

    if (sw_http_is(request, "POST", "/upload")) {
        const b8 body_pending = request->body_pending;
        sw_http_upload_field fields[] = {
            { "note", state->upload_note, sizeof(state->upload_note) }
        };

        assert(state->upload_path != NULL);
        if (state->expect_upload_failure) {
            assert(sw_http_upload_save_fields(connection, request, "file", sw_test_upload_path, fields, 1, &state->uploaded_size, state) != 0);
            state->upload_body_pending = body_pending;
            sw_http_replyf(connection, 400, "text/plain; charset=utf-8",
                "upload failed body_pending=%d",
                state->upload_body_pending);
            return;
        }

        assert(sw_http_upload_save_fields(connection, request, "file", sw_test_upload_path, fields, 1, &state->uploaded_size, state) == 0);
        state->upload_body_pending = body_pending;
        state->upload_seen_file = 1;
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8",
            "note=%s file=%s size=%zu body_pending=%d",
            state->upload_note,
            state->uploaded_name,
            state->uploaded_size,
            state->upload_body_pending);
        return;
    }

    if (sw_http_is(request, "GET", "/cookies")) {
        char theme[32];
        char quoted[64];
        sw_http_cookie strict_cookie = sw_http_cookie_default();

        strict_cookie.same_site = SW_COOKIE_SAMESITE_STRICT;
        assert(sw_http_get_cookie(request, "theme", theme, sizeof(theme)) >= 0);
        assert(sw_http_get_cookie(request, "quoted", quoted, sizeof(quoted)) >= 0);
        assert(sw_http_set_cookie(connection, "seen", "yes", NULL) == 0);
        assert(sw_http_set_cookie(connection, "mode", "strict", &strict_cookie) == 0);
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "theme=%s quoted=%s", theme, quoted);
        return;
    }

    if (sw_http_is(request, "GET", "/clear-cookie")) {
        assert(sw_http_clear_cookie(connection, "seen", NULL) == 0);
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "cleared");
        return;
    }

    if (sw_http_is(request, "GET", "/redirect")) {
        assert(sw_http_set_header(connection, "X-Syphax-Test", "redirect") == 0);
        assert(sw_http_set_cookie(connection, "flash", "1", NULL) == 0);
        assert(sw_http_redirect(connection, "/target") == 0);
        return;
    }

    if (sw_http_is(request, "GET", "/session")) {
        sw_session* session;
        const c8* hits_text;
        i32 hits;
        char hits_buffer[32];

        assert(state->sessions != NULL);
        session = sw_sessions_start(state->sessions, connection, request);
        assert(session != NULL);
        hits_text = sw_session_get(session, "hits");
        hits = hits_text != NULL ? atoi(hits_text) : 0;
        hits += 1;
        assert(snprintf(hits_buffer, sizeof(hits_buffer), "%d", hits) > 0);
        assert(sw_session_set(session, "hits", hits_buffer) == 0);
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "id=%s hits=%d", sw_session_id(session), hits);
        return;
    }

    if (sw_http_is(request, "GET", "/session-end")) {
        assert(state->sessions != NULL);
        assert(sw_sessions_end(state->sessions, connection, request) == 0);
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "ended");
        return;
    }

    if (sw_http_is(request, "GET", "/token-login")) {
        sw_token* token;

        assert(state->tokens != NULL);
        token = sw_tokens_login(state->tokens, connection, request, "user-1");
        assert(token != NULL);
        assert(sw_token_set(token, "role", "admin") == 0);
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8",
            "login id=%s user_id=%s role=%s",
            sw_token_id(token),
            sw_token_get(token, "user_id"),
            sw_token_get(token, "role"));
        return;
    }

    if (sw_http_is(request, "GET", "/token-items")) {
        sw_token* token;
        const i32 first = 0;
        const i32 second = 0;
        i32 third;
        i32 removed;
        i32 third_after_remove;

        assert(state->tokens != NULL);
        token = sw_tokens_login(state->tokens, connection, request, NULL);
        assert(token != NULL);
        assert(sw_token_set(token, "first", "1") == first);
        assert(sw_token_set(token, "second", "2") == second);
        third = sw_token_set(token, "third", "3");
        removed = sw_token_remove(token, "first");
        third_after_remove = sw_token_set(token, "third", "3");
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8",
            "first=%d second=%d third=%d removed=%d third_after_remove=%d",
            first,
            second,
            third,
            removed,
            third_after_remove);
        return;
    }

    if (sw_http_is(request, "GET", "/token-current")) {
        sw_token* token;
        const c8* user_id;
        const c8* role;

        assert(state->tokens != NULL);
        token = sw_tokens_current(state->tokens, connection, request);
        if (token == NULL) {
            sw_http_replyf(connection, 401, "text/plain; charset=utf-8", "no token");
            return;
        }
        user_id = sw_token_get(token, "user_id");
        role = sw_token_get(token, "role");
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8",
            "current id=%s user_id=%s role=%s",
            sw_token_id(token),
            user_id != NULL ? user_id : "",
            role != NULL ? role : "");
        return;
    }

    if (sw_http_is(request, "GET", "/token-logout")) {
        assert(state->tokens != NULL);
        assert(sw_tokens_logout(state->tokens, connection, request) == 0);
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "logout");
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

    if (sw_http_is(request, "GET", "/cookie")) {
        assert(sw_http_set_cookie(connection, "tls_cookie", "1", NULL) == 0);
        sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "cookie");
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
    assert(strcmp(sw_translate(translator, "Queue"), "Queue") == 0);
    assert(sw_translator_set_language(translator, "ja"));
    assert(strcmp(sw_translate(translator, "Queue"), "キュー") == 0);
    assert(sw_translator_set_language(translator, "ar"));
    assert(strcmp(sw_translate(translator, "Queue"), "قائمة الانتظار") == 0);
    assert(sw_translator_set_language(translator, "fa"));
    assert(strcmp(sw_translate(translator, "Queue"), "صف") == 0);
    assert(sw_translator_set_language(translator, "zh"));
    assert(strcmp(sw_translate(translator, "Queue"), "队列") == 0);
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
    assert(strcmp(sw_translate(auto_loaded, "Queue"), "キュー") == 0);
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
        sw_attr("title", "Queue"),
        sw_attr("aria-label", "Queue"),
        (sw_attr_item){ .name = "alt", .value = "Queue", .enabled = 1, .no_translate = 1 }
    )));
    assert(sw_void(h, "input", sw_attrs(
        sw_attr("type", "text"),
        sw_attr("placeholder", "Queue"),
        sw_attr("value", "\"quoted\"")
    )));
    assert(sw_text(h, "Queue"));
    assert(sw_text_no_translate(h, "Queue"));
    assert(sw_end(h, "div"));

    assert(strstr(sw_buffer_data(h), "title=\"队列\"") != NULL);
    assert(strstr(sw_buffer_data(h), "placeholder=\"队列\"") != NULL);
    assert(strstr(sw_buffer_data(h), "aria-label=\"队列\"") != NULL);
    assert(strstr(sw_buffer_data(h), "alt=\"Queue\"") != NULL);
    assert(strstr(sw_buffer_data(h), "value=\"&quot;quoted&quot;\"") != NULL);
    assert(strstr(sw_buffer_data(h), "队列Queue") != NULL);
    assert(strstr(sw_buffer_data(h), "</input>") == NULL);

    sw_buffer_reset(h);
    assert(sw_buffer_translation_enabled(h));
    sw_translate_off(h);
    assert(!sw_buffer_translation_enabled(h));
    assert(sw_tag(h, "div", sw_attrs(sw_attr("title", "Queue"))));
    assert(sw_text(h, "Queue"));
    sw_translate_on(h);
    assert(sw_buffer_translation_enabled(h));
    assert(sw_text(h, "Queue"));
    assert(sw_end(h, "div"));
    html = sw_buffer_data(h);
    assert(strstr(html, "title=\"Queue\"") != NULL);
    assert(strstr(html, ">Queue队列</div>") != NULL);

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
        sw_attr("title", "Queue")
    )));
    assert(sw_text(h, "Queue"));
    assert(sw_tag(h, "span", sw_attrs(
        sw_attr_translation(1),
        sw_attr("title", "Queue")
    )));
    assert(sw_text(h, "Queue"));
    assert(sw_end(h, "span"));
    assert(sw_text(h, "Queue"));
    assert(sw_end(h, "div"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<div title=\"Queue\">Queue<span title=\"队列\">队列</span>Queue</div>") != NULL);

    sw_buffer_reset(h);
    assert(sw_tag(h, "div", sw_attrs(
        sw_attr("style", "border-color:currentColor"),
        sw_attr_direction(SW_LANGUAGE_DIRECTION_TTB)
    )));
    assert(sw_text(h, "Queue"));
    assert(sw_end(h, "div"));
    html = sw_buffer_data(h);
    assert(strstr(html, "<div dir=\"ltr\" data-sw-direction=\"ttb\" style=\"border-color:currentColor;writing-mode:vertical-rl;text-orientation:mixed;\">队列</div>") != NULL);

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

    assert(sw_title(h, "Queue"));
    assert(sw_title_no_translate(h, "Queue"));
    sw_section(h, sw_attrs(
        sw_attr("class", "shell"),
        sw_attr("data-mode", "demo"),
        sw_attr("data-label", "Queue"),
        sw_attr_bool("hidden", 1),
        sw_attr_bool("selected", 1)
    ), {
        sw_input(h, sw_attrs(
            sw_attr("type", "text"),
            sw_attr("placeholder", "Queue"),
            sw_attr("data-role", "search"),
            sw_attr_bool("disabled", 1)
        ));
        sw_text(h, "Queue");
        sw_text_no_translate(h, "Queue");
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
    assert(strstr(html, "<title>队列</title>") != NULL);
    assert(strstr(html, "<title>Queue</title>") != NULL);
    assert(strstr(html, "data-mode=\"demo\"") != NULL);
    assert(strstr(html, "data-label=\"Queue\"") != NULL);
    assert(strstr(html, "placeholder=\"队列\"") != NULL);
    assert(strstr(html, "data-role=\"search\"") != NULL);
    assert(strstr(html, "hidden") != NULL);
    assert(strstr(html, "selected") != NULL);
    assert(strstr(html, "disabled") != NULL);
    assert(strstr(html, "</input>") == NULL);
    assert(strstr(html, "队列Queue") != NULL);
    assert(strstr(html, "<a href=\"/docs\">文档</a>") != NULL);
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
    sw_http_header form_headers[] = {
        { "Content-Type", "application/x-www-form-urlencoded" },
        { "Cookie", "theme=dark; empty=; quoted=\"light mode\"; encoded=A%2FB" },
        { "Cookie", "second=two" }
    };
    sw_http_header multipart_headers[] = {
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
    const c8 duplicate_header_multipart_body[] =
        "--demo\r\n"
        "Content-Disposition: form-data; name=\"old\"; filename=\"old.txt\"\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"hello.txt\"\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "x\r\n"
        "--demo--\r\n";
    const sw_http_message message = {
        .method = "POST",
        .uri = "/upload?q=Jane+Doe&empty=&encoded=A%2FB",
        .proto = "HTTP/1.1",
        .headers = form_headers,
        .num_headers = 3,
        .body = "name=Jane+Doe&city=Tokyo",
        .body_len = strlen("name=Jane+Doe&city=Tokyo"),
        .content_length = strlen("name=Jane+Doe&city=Tokyo")
    };
    sw_http_message multipart_message = {
        .method = "POST",
        .uri = "/upload",
        .proto = "HTTP/1.1",
        .headers = multipart_headers,
        .num_headers = 1,
        .body = multipart_body,
        .body_len = sizeof(multipart_body) - 1,
        .content_length = sizeof(multipart_body) - 1
    };
    sw_http_message duplicate_header_multipart_message = {
        .method = "POST",
        .uri = "/upload",
        .proto = "HTTP/1.1",
        .headers = multipart_headers,
        .num_headers = 1,
        .body = duplicate_header_multipart_body,
        .body_len = sizeof(duplicate_header_multipart_body) - 1,
        .content_length = sizeof(duplicate_header_multipart_body) - 1
    };
    char value[64];
    sz offset = 0;
    sw_http_multipart part;

    assert(sw_http_is(&message, "POST", "/upload"));
    assert(!sw_http_is(&message, "GET", "/upload"));
    assert(!sw_http_is(&message, "POST", "/other"));
    assert(sw_http_path_is(&message, "/upload"));
    assert(sw_http_path_is(&multipart_message, "/upload"));
    assert(sw_http_path_starts(&message, "/upl"));
    assert(!sw_http_path_is(&message, "/upload?q=Jane+Doe"));
    assert(!sw_http_path_starts(&message, "/uploads"));
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
    assert(sw_http_get_form(&multipart_message, "note", value, sizeof(value)) > 0);
    assert(strcmp(value, "hello") == 0);
    assert(sw_http_get_cookie(&message, "theme", value, sizeof(value)) > 0);
    assert(strcmp(value, "dark") == 0);
    assert(sw_http_get_cookie(&message, "quoted", value, sizeof(value)) > 0);
    assert(strcmp(value, "light mode") == 0);
    assert(sw_http_get_cookie(&message, "empty", value, sizeof(value)) == 0);
    assert(strcmp(value, "") == 0);
    assert(sw_http_get_cookie(&message, "encoded", value, sizeof(value)) > 0);
    assert(strcmp(value, "A%2FB") == 0);
    assert(sw_http_get_cookie(&message, "second", value, sizeof(value)) > 0);
    assert(strcmp(value, "two") == 0);
    assert(sw_http_get_cookie(&message, "missing", value, sizeof(value)) == 0);
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

    offset = 0;
    memset(&part, 0, sizeof(part));
    assert(sw_http_next_multipart(&duplicate_header_multipart_message, &part, &offset) == 1);
    assert(strcmp(part.name, "file") == 0);
    assert(strcmp(part.filename, "hello.txt") == 0);
    assert(strcmp(part.content_type, "application/octet-stream") == 0);
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
        "examples/06_session_login.c",
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
            response.size = response_len;
            if (response_has_complete_body(&response)) {
                break;
            }
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
            response.size = response_len;
            if (response_has_complete_body(&response)) {
                break;
            }
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
            response.size = response_len;
            if (response_has_complete_body(&response)) {
                break;
            }
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

static c8 upload_fixture_byte(sz index) {
    return (c8)('A' + (index % 23));
}

static sz upload_fixture_file_size(void) {
    const char* value = getenv("SW_TEST_UPLOAD_BYTES");
    char* end = NULL;
    unsigned long long parsed;

    if (value == NULL || *value == '\0') {
        return (3 * 1024 * 1024) + 123;
    }

    errno = 0;
    parsed = strtoull(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || parsed == 0 || parsed > (unsigned long long)((sz)-1)) {
        return (3 * 1024 * 1024) + 123;
    }
    return (sz)parsed;
}

static void send_all_polled(sw_mgr* mgr, sw_test_socket fd, const void* data, sz data_len) {
    const c8* cursor = (const c8*)data;
    sz sent_total = 0;

    while (sent_total < data_len) {
        const sz remaining = data_len - sent_total;
        const int send_len = remaining > SW_TEST_TRANSFER_CHUNK_BYTES ? SW_TEST_TRANSFER_CHUNK_BYTES : (int)remaining;
        const int sent = (int)send(fd, cursor + sent_total, send_len, 0);

        if (sent > 0) {
            sent_total += (sz)sent;
            assert(sw_mgr_poll(mgr, 1) >= 0);
            continue;
        }

#ifdef _WIN32
        assert(WSAGetLastError() == WSAEWOULDBLOCK);
#else
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
#endif
        assert(sw_mgr_poll(mgr, 5) >= 0);
        sw_test_sleep_ms(1);
    }
}

typedef struct {
    sw_test_socket fd;
    const c8* header;
    const c8* prefix;
    const c8* suffix;
    sz file_size;
    b8 ok;
} sw_upload_sender;

static b8 send_all_socket(sw_test_socket fd, const void* data, sz data_len) {
    const c8* cursor = (const c8*)data;
    sz sent_total = 0;

    while (sent_total < data_len) {
        const sz remaining = data_len - sent_total;
        const int send_len = remaining > SW_TEST_TRANSFER_CHUNK_BYTES ? SW_TEST_TRANSFER_CHUNK_BYTES : (int)remaining;
        const int sent = (int)send(fd, cursor + sent_total, send_len, 0);

        if (sent > 0) {
            sent_total += (sz)sent;
            continue;
        }

#ifdef _WIN32
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
            return 0;
        }
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            return 0;
        }
#endif
        s_thread_sleep_ms(1);
    }

    return 1;
}

static b8 send_upload_fixture_bytes_socket(sw_test_socket fd, sz file_size) {
    c8 chunk[SW_TEST_TRANSFER_CHUNK_BYTES];
    sz written = 0;

    while (written < file_size) {
        const sz chunk_len = (file_size - written) < sizeof(chunk) ? (file_size - written) : sizeof(chunk);
        sz i;

        for (i = 0; i < chunk_len; ++i) {
            chunk[i] = upload_fixture_byte(written + i);
        }
        if (!send_all_socket(fd, chunk, chunk_len)) {
            return 0;
        }
        written += chunk_len;
    }

    return 1;
}

static void* upload_sender_main(void* arg) {
    sw_upload_sender* sender = (sw_upload_sender*)arg;

    sender->ok = send_all_socket(sender->fd, sender->header, strlen(sender->header))
        && send_all_socket(sender->fd, sender->prefix, strlen(sender->prefix))
        && send_upload_fixture_bytes_socket(sender->fd, sender->file_size)
        && send_all_socket(sender->fd, sender->suffix, strlen(sender->suffix));

    return NULL;
}

static b8 file_exists(const c8* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    fclose(file);
    return 1;
}

static void assert_upload_fixture_file(const c8* path, sz expected_size) {
    FILE* file = fopen(path, "rb");
    c8 chunk[SW_TEST_TRANSFER_CHUNK_BYTES];
    sz checked = 0;

    assert(file != NULL);
    while (checked < expected_size) {
        const sz wanted = (expected_size - checked) < sizeof(chunk) ? (expected_size - checked) : sizeof(chunk);
        const sz read_bytes = fread(chunk, 1, wanted, file);
        sz i;

        assert(read_bytes == wanted);
        for (i = 0; i < read_bytes; ++i) {
            assert(chunk[i] == upload_fixture_byte(checked + i));
        }
        checked += read_bytes;
    }
    assert(fgetc(file) == EOF);
    fclose(file);
}

static b8 wait_socket_closed(sw_mgr* mgr, sw_test_socket fd, int max_attempts, i32 poll_ms, i32 sleep_ms) {
    int attempts;

    for (attempts = 0; attempts < max_attempts; ++attempts) {
        char chunk[256];
        int received;
        if (sleep_ms > 0) {
            sw_test_sleep_ms(sleep_ms);
        }
        assert(sw_mgr_poll(mgr, poll_ms) >= 0);
        received = (int)recv(fd, chunk, sizeof(chunk), 0);
        if (received == 0) {
            return 1;
        }
        if (received > 0) {
            continue;
        }
#ifdef _WIN32
        if (WSAGetLastError() != WSAEWOULDBLOCK) {
#else
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
#endif
            return 1;
        }
    }
    return 0;
}

static sw_test_response read_response_background(sw_test_socket fd, int max_attempts, i32 sleep_ms) {
    sw_test_response response = {0};
    sz response_len = 0;
    int attempts;

    response.data = (char*)calloc(1, 65536);
    assert(response.data != NULL);
    for (attempts = 0; attempts < max_attempts; ++attempts) {
        char chunk[4096];
        const int received = (int)recv(fd, chunk, sizeof(chunk), 0);
        if (received > 0) {
            memcpy(response.data + response_len, chunk, (sz)received);
            response_len += (sz)received;
            response.data[response_len] = '\0';
            response.size = response_len;
            if (response_has_complete_body(&response)) {
                break;
            }
            continue;
        }
        if (received == 0) {
            response.closed = 1;
            break;
        }
#ifdef _WIN32
        assert(WSAGetLastError() == WSAEWOULDBLOCK);
#else
        assert(errno == EAGAIN || errno == EWOULDBLOCK);
#endif
        sw_test_sleep_ms(sleep_ms);
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

static b8 response_has_complete_body(const sw_test_response* response) {
    return response != NULL
        && response->data != NULL
        && strstr(response->data, "\r\n\r\n") != NULL
        && strstr(response->data, "Content-Length:") != NULL
        && response_content_length(response) == response_body_size(response);
}

static void assert_complete_response(const sw_test_response* response) {
    assert(response_has_complete_body(response));
}

static void response_cookie_value(const sw_test_response* response, const char* name, char* out, sz out_len) {
    const char* cursor = response->data;
    const sz name_len = strlen(name);

    assert(out != NULL);
    assert(out_len > 0);
    out[0] = '\0';

    while ((cursor = strstr(cursor, "Set-Cookie: ")) != NULL) {
        const char* value_begin = cursor + strlen("Set-Cookie: ");
        const char* equals = strchr(value_begin, '=');
        const char* value_end;
        sz value_len;

        if (equals != NULL && (sz)(equals - value_begin) == name_len && strncmp(value_begin, name, name_len) == 0) {
            value_begin = equals + 1;
            value_end = strchr(value_begin, ';');
            if (value_end == NULL) {
                value_end = strstr(value_begin, "\r\n");
            }
            assert(value_end != NULL);
            value_len = (sz)(value_end - value_begin);
            assert(value_len + 1 < out_len);
            memcpy(out, value_begin, value_len);
            out[value_len] = '\0';
            return;
        }
        cursor = value_begin;
    }

    assert(!"missing Set-Cookie header");
}

#if defined(SYPHAX_WEB_HAS_CRYPTO)
static void response_body_field(const sw_test_response* response, const char* name, char* out, sz out_len) {
    const char* body;
    const char* begin;
    const char* end;
    sz value_len;

    assert(response != NULL);
    assert(name != NULL);
    assert(out != NULL);
    assert(out_len > 0);
    out[0] = '\0';

    body = strstr(response->data, "\r\n\r\n");
    assert(body != NULL);
    body += 4;
    begin = strstr(body, name);
    assert(begin != NULL);
    begin += strlen(name);
    end = begin;
    while (*end != '\0' && *end != ' ' && *end != '\r' && *end != '\n') {
        ++end;
    }
    value_len = (sz)(end - begin);
    assert(value_len + 1 < out_len);
    memcpy(out, begin, value_len);
    out[value_len] = '\0';
}

static void tamper_cookie_value(const char* in, char* out, sz out_len) {
    sz len;
    sz i;

    assert(in != NULL);
    assert(out != NULL);
    assert(out_len > strlen(in));
    strcpy(out, in);
    len = strlen(out);
    for (i = 3; i < len; ++i) {
        if (out[i] >= 'A' && out[i] <= 'Y') {
            out[i] = (c8)(out[i] + 1);
            return;
        }
        if (out[i] == 'Z') {
            out[i] = 'A';
            return;
        }
        if (out[i] >= 'a' && out[i] <= 'y') {
            out[i] = (c8)(out[i] + 1);
            return;
        }
        if (out[i] == 'z') {
            out[i] = 'a';
            return;
        }
        if (out[i] >= '0' && out[i] <= '8') {
            out[i] = (c8)(out[i] + 1);
            return;
        }
        if (out[i] == '9') {
            out[i] = '0';
            return;
        }
        if (out[i] == '-') {
            out[i] = '_';
            return;
        }
        if (out[i] == '_') {
            out[i] = '-';
            return;
        }
    }
    assert(!"cookie value did not contain a mutable payload character");
}
#endif

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
    char upload_name[256];
    char upload_path[512];
    char request_buffer[1024];
    char upload_data[5];
    FILE* file;
    FILE* upload_file;
    u16 port;
    sw_test_response response;
    sw_test_socket keep_fd;
    int listen_rc;

    assert(mgr != NULL);

    create_temp_directory(docroot_path, sizeof(docroot_path), "assets");

    unique_name("fixture.txt", file_name, sizeof(file_name));
    assert(snprintf(file_path, sizeof(file_path), "%s/%s", docroot_path, file_name) > 0);
    file = fopen(file_path, "wb");
    assert(file != NULL);
    fputs("public payload", file);
    fclose(file);

    memset(&state, 0, sizeof(state));
    state.docroot = docroot_path;
    state.file_name = file_name;

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

    response = issue_request(mgr, port,
        "POST /form HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "4\r\n"
        "name\r\n"
        "1\r\n"
        "=\r\n"
        "4;ext=ok\r\n"
        "Jane\r\n"
        "4\r\n"
        "+Doe\r\n"
        "0\r\n"
        "X-Test-Trailer: yes\r\n"
        "\r\n");
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

    keep_fd = connect_to_port(port);
    assert(send(keep_fd,
        "POST /form HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: application/x-www-form-urlencoded\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "d\r\n"
        "name=Jane+Doe\r\n"
        "0\r\n"
        "\r\n",
        (int)strlen(
            "POST /form HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "d\r\n"
            "name=Jane+Doe\r\n"
            "0\r\n"
            "\r\n"),
        0) == (int)strlen(
            "POST /form HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: application/x-www-form-urlencoded\r\n"
            "Transfer-Encoding: chunked\r\n"
            "\r\n"
            "d\r\n"
            "name=Jane+Doe\r\n"
            "0\r\n"
            "\r\n"));
    response = read_response_from_socket(mgr, keep_fd, 80, 5, 1);
    assert_complete_response(&response);
    assert(!response.closed);
    assert(strstr(response.data, "name=Jane Doe") != NULL);
    assert(strstr(response.data, "Connection: keep-alive") != NULL);
    free(response.data);

    assert(send(keep_fd,
        "GET /health-missing HTTP/1.1\r\nHost: localhost\r\n\r\n",
        (int)strlen("GET /health-missing HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        0) == (int)strlen("GET /health-missing HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    response = read_response_from_socket(mgr, keep_fd, 80, 5, 1);
    assert_complete_response(&response);
    assert(!response.closed);
    assert(strstr(response.data, "HTTP/1.1 404 Not Found") != NULL);
    assert(strstr(response.data, "Connection: keep-alive") != NULL);
    free(response.data);

    assert(send(keep_fd,
        "GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n",
        (int)strlen("GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        0) == (int)strlen("GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    response = read_response_from_socket(mgr, keep_fd, 80, 5, 1);
    assert_complete_response(&response);
    assert(strstr(response.data, "Content-Type: text/css; charset=utf-8") != NULL);
    assert(strstr(response.data, "body { color: #fff; }") != NULL);
    free(response.data);

    assert(send(keep_fd,
        "GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        (int)strlen("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"),
        0) == (int)strlen("GET / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"));
    response = read_response_from_socket(mgr, keep_fd, 80, 5, 1);
    assert_complete_response(&response);
    assert(strstr(response.data, "Connection: close") != NULL);
    free(response.data);
    assert(wait_socket_closed(mgr, keep_fd, 80, 5, 1));
#ifdef _WIN32
    closesocket(keep_fd);
#else
    close(keep_fd);
#endif

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

    unique_name("small-upload.bin", upload_name, sizeof(upload_name));
    temp_path(upload_path, sizeof(upload_path), upload_name);
    remove(upload_path);
    state.upload_path = upload_path;
    state.uploaded_name[0] = '\0';
    state.upload_note[0] = '\0';
    state.uploaded_size = 0;
    state.upload_seen_file = 0;

    {
        const c8 small_upload_body[] =
        "--small\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"small.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n"
        "abcde\r\n"
        "--small\r\n"
        "Content-Disposition: form-data; name=\"note\"\r\n"
        "\r\n"
        "small file\r\n"
        "--small--\r\n";

        assert(snprintf(request_buffer, sizeof(request_buffer),
            "POST /upload HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "Content-Type: multipart/form-data; boundary=small\r\n"
            "Content-Length: %zu\r\n"
            "\r\n"
            "%s", strlen(small_upload_body), small_upload_body) > 0);
        response = issue_request_bytes(mgr, port, request_buffer, strlen(request_buffer));
    }
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response.data, "file=small.bin") != NULL);
    assert(strstr(response.data, "note=small file") != NULL);
    assert(strstr(response.data, "body_pending=0") != NULL);
    assert(state.upload_seen_file);
    assert(!state.upload_body_pending);
    assert(state.uploaded_size == 5);
    assert(strcmp(state.uploaded_name, "small.bin") == 0);
    assert(strcmp(state.upload_note, "small file") == 0);
    free(response.data);

    upload_file = fopen(upload_path, "rb");
    assert(upload_file != NULL);
    assert(fread(upload_data, 1, sizeof(upload_data), upload_file) == sizeof(upload_data));
    assert(fclose(upload_file) == 0);
    assert(memcmp(upload_data, "abcde", sizeof(upload_data)) == 0);
    remove(upload_path);
    state.upload_path = NULL;

    assert(state.request_count == 16);
    remove(file_path);
#ifdef _WIN32
    RemoveDirectoryA(docroot_path);
#else
    rmdir(docroot_path);
#endif
    sw_mgr_destroy(mgr);
}

static void test_cookie_helpers(void) {
    sw_mgr* mgr = sw_mgr_create(NULL);
    sw_test_server_state state = {0};
    u16 port;
    sw_test_response response;

    assert(mgr != NULL);
    assert(sw_http_listen(mgr, "http://127.0.0.1:0", sw_test_handler, &state) == 0);
    port = sw_mgr_get_listener_port(mgr, 0);
    assert(port != 0);

    response = issue_request(mgr, port,
        "GET /cookies HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: theme=dark; empty=; quoted=\"light mode\"\r\n"
        "\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "theme=dark quoted=light mode") != NULL);
    assert(strstr(response.data, "Set-Cookie: seen=yes; Path=/; HttpOnly; SameSite=Lax") != NULL);
    assert(strstr(response.data, "Set-Cookie: mode=strict; Path=/; HttpOnly; SameSite=Strict") != NULL);
    assert(strstr(response.data, "Set-Cookie: seen=yes; Path=/; Secure") == NULL);
    free(response.data);

    response = issue_request(mgr, port, "GET /clear-cookie HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "Set-Cookie: seen=; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Path=/; HttpOnly; SameSite=Lax") != NULL);
    free(response.data);

    sw_mgr_destroy(mgr);
}

static void test_response_helpers(void) {
    sw_mgr* mgr = sw_mgr_create(NULL);
    sw_test_server_state state = {0};
    sw_test_response response;
    u16 port;

    assert(mgr != NULL);
    assert(sw_http_listen(mgr, "http://127.0.0.1:0", sw_test_handler, &state) == 0);
    port = sw_mgr_get_listener_port(mgr, 0);
    assert(port != 0);

    response = issue_request(mgr, port, "GET /redirect HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 303 See Other") != NULL);
    assert(strstr(response.data, "Location: /target") != NULL);
    assert(strstr(response.data, "X-Syphax-Test: redirect") != NULL);
    assert(strstr(response.data, "Set-Cookie: flash=1; Path=/; HttpOnly; SameSite=Lax") != NULL);
    assert(strstr(response.data, "Content-Length: 0") != NULL);
    free(response.data);

    sw_mgr_destroy(mgr);
}

static void test_large_multipart_upload(void) {
    const char boundary[] = "large-upload-boundary";
    const char prefix[] =
        "--large-upload-boundary\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"big.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n";
    const char suffix[] =
        "\r\n--large-upload-boundary\r\n"
        "Content-Disposition: form-data; name=\"note\"\r\n"
        "\r\n"
        "large file\r\n"
        "--large-upload-boundary--\r\n";
    const sz file_size = upload_fixture_file_size();
    const sz body_len = (sizeof(prefix) - 1) + file_size + (sizeof(suffix) - 1);
    sw_server_config config = sw_server_config_default();
    sw_mgr* mgr;
    sw_test_server_state state = {0};
    sw_test_socket fd;
    sw_test_response response;
    sw_upload_sender sender;
    s_thread sender_thread;
    char upload_name[256];
    char upload_path[512];
    char request_header[1024];
    u16 port;

    config.max_body_bytes = body_len + 1024;
    config.max_read_buffer_bytes = 32 * 1024;
    config.body_timeout_ms = 120 * 1000;

    unique_name("upload.bin", upload_name, sizeof(upload_name));
    temp_path(upload_path, sizeof(upload_path), upload_name);
    remove(upload_path);
    state.upload_path = upload_path;

    mgr = sw_mgr_create(&config);
    assert(mgr != NULL);
    assert(sw_http_listen(mgr, "http://127.0.0.1:0", sw_test_handler, &state) == 0);
    port = sw_mgr_get_listener_port(mgr, 0);
    assert(port != 0);

    fd = connect_to_port(port);
    assert(snprintf(request_header, sizeof(request_header),
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: multipart/form-data; boundary=%s\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        boundary,
        body_len) > 0);

    memset(&sender, 0, sizeof(sender));
    sender.fd = fd;
    sender.header = request_header;
    sender.prefix = prefix;
    sender.suffix = suffix;
    sender.file_size = file_size;
    assert(s_thread_create(&sender_thread, upload_sender_main, &sender));

    response = read_response_from_socket(mgr, fd, 200, 5, 1);
    assert(s_thread_join(&sender_thread, NULL));
    assert(sender.ok);
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response.data, "file=big.bin") != NULL);
    assert(strstr(response.data, "note=large file") != NULL);
    assert(strstr(response.data, "body_pending=1") != NULL);
    assert(state.upload_seen_file);
    assert(state.upload_body_pending);
    assert(state.uploaded_size == file_size);
    assert(strcmp(state.uploaded_name, "big.bin") == 0);
    assert(strcmp(state.upload_note, "large file") == 0);
    free(response.data);

    assert_upload_fixture_file(upload_path, file_size);

    send_all_polled(mgr, fd, "GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n", strlen("GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    response = read_response_from_socket(mgr, fd, 120, 5, 1);
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response.data, "Content-Type: text/css; charset=utf-8") != NULL);
    free(response.data);

#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    remove(upload_path);
    sw_mgr_destroy(mgr);
}

static void test_large_multipart_upload_failure_keeps_path(void) {
    const char boundary[] = "large-upload-boundary";
    const char prefix[] =
        "--large-upload-boundary\r\n"
        "Content-Disposition: form-data; name=\"file\"; filename=\"broken.bin\"\r\n"
        "Content-Type: application/octet-stream\r\n"
        "\r\n";
    const char broken_suffix[] = "\r\nthis-is-not-a-boundary";
    const sz file_size = 4096;
    const sz body_len = (sizeof(prefix) - 1) + file_size + (sizeof(broken_suffix) - 1);
    sw_server_config config = sw_server_config_default();
    sw_mgr* mgr;
    sw_test_server_state state = {0};
    sw_test_socket fd;
    sw_test_response response;
    sw_upload_sender sender;
    s_thread sender_thread;
    char upload_name[256];
    char upload_path[512];
    char request_header[1024];
    FILE* existing;
    u16 port;

    config.max_body_bytes = body_len + 1024;
    config.max_read_buffer_bytes = 4096;
    config.body_timeout_ms = 1000;

    unique_name("upload.bin", upload_name, sizeof(upload_name));
    temp_path(upload_path, sizeof(upload_path), upload_name);
    existing = fopen(upload_path, "wb");
    assert(existing != NULL);
    fputs("existing caller file", existing);
    fclose(existing);

    state.upload_path = upload_path;
    state.expect_upload_failure = 1;

    mgr = sw_mgr_create(&config);
    assert(mgr != NULL);
    assert(sw_http_listen(mgr, "http://127.0.0.1:0", sw_test_handler, &state) == 0);
    port = sw_mgr_get_listener_port(mgr, 0);
    assert(port != 0);

    fd = connect_to_port(port);
    assert(snprintf(request_header, sizeof(request_header),
        "POST /upload HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Content-Type: multipart/form-data; boundary=%s\r\n"
        "Content-Length: %zu\r\n"
        "\r\n",
        boundary,
        body_len) > 0);

    memset(&sender, 0, sizeof(sender));
    sender.fd = fd;
    sender.header = request_header;
    sender.prefix = prefix;
    sender.suffix = broken_suffix;
    sender.file_size = file_size;
    assert(s_thread_create(&sender_thread, upload_sender_main, &sender));

    response = read_response_from_socket(mgr, fd, 120, 5, 1);
    assert(s_thread_join(&sender_thread, NULL));
    assert(sender.ok);
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 400 Bad Request") != NULL);
    assert(strstr(response.data, "upload failed") != NULL);
    free(response.data);

    assert(file_exists(upload_path));

#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif
    remove(upload_path);
    sw_mgr_destroy(mgr);
}

static void test_session_helpers(void) {
    sw_session_config config = sw_session_config_default();
    sw_mgr* mgr = sw_mgr_create(NULL);
    sw_test_server_state state = {0};
    u16 port;
    sw_test_response response;
    char session_id[128];
    char request[512];

    config.cookie_name = "sid";
    config.ttl_seconds = 60;
    config.max_sessions = 2;
    config.max_items = 2;
    state.sessions = sw_sessions_create(&config);

    assert(mgr != NULL);
    assert(state.sessions != NULL);
    assert(sw_http_listen(mgr, "http://127.0.0.1:0", sw_test_handler, &state) == 0);
    port = sw_mgr_get_listener_port(mgr, 0);
    assert(port != 0);

    response = issue_request(mgr, port, "GET /session HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "hits=1") != NULL);
    assert(strstr(response.data, "Set-Cookie: sid=") != NULL);
    assert(strstr(response.data, "; Max-Age=60; Path=/; HttpOnly; SameSite=Lax") != NULL);
    response_cookie_value(&response, "sid", session_id, sizeof(session_id));
    assert(strlen(session_id) == 64);
    free(response.data);

    assert(snprintf(request, sizeof(request),
        "GET /session HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: sid=%s\r\n"
        "\r\n",
        session_id) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    assert(strstr(response.data, "hits=2") != NULL);
    assert(strstr(response.data, session_id) != NULL);
    free(response.data);

    assert(snprintf(request, sizeof(request),
        "GET /session-end HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: sid=%s\r\n"
        "\r\n",
        session_id) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    assert(strstr(response.data, "Set-Cookie: sid=; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Path=/; HttpOnly; SameSite=Lax") != NULL);
    free(response.data);

    sw_mgr_destroy(mgr);
    sw_sessions_destroy(state.sessions);
}

static void test_token_helpers(void) {
#if defined(SYPHAX_WEB_HAS_CRYPTO)
    static const u8 secret[32] = {
        0x10, 0x21, 0x32, 0x43, 0x54, 0x65, 0x76, 0x87,
        0x98, 0xa9, 0xba, 0xcb, 0xdc, 0xed, 0xfe, 0x0f,
        0xf0, 0xe1, 0xd2, 0xc3, 0xb4, 0xa5, 0x96, 0x87,
        0x78, 0x69, 0x5a, 0x4b, 0x3c, 0x2d, 0x1e, 0x0f
    };
    sw_token_config bad_config = sw_token_config_default();
    sw_tokens* defaults = sw_tokens_create(NULL);
    sw_token_config config = sw_token_config_default();
    sw_mgr* mgr = sw_mgr_create(NULL);
    sw_test_server_state state = {0};
    u16 port;
    sw_test_response response;
    char token_id[128];
    char token_cookie[256];
    char refreshed_cookie[256];
    char replacement_cookie[256];
    char second_cookie[256];
    char third_cookie[256];
    char request[1024];
    char bad_cookie[256];

    assert(defaults != NULL);
    sw_tokens_destroy(defaults);

    bad_config.secret = secret;
    bad_config.secret_len = 31;
    assert(sw_tokens_create(&bad_config) == NULL);

    config.cookie_name = "tok";
    config.secret = secret;
    config.secret_len = sizeof(secret);
    config.ttl_seconds = 60;
    config.max_tokens = 8;
    config.max_items = 2;
    state.tokens = sw_tokens_create(&config);

    assert(mgr != NULL);
    assert(state.tokens != NULL);
    assert(sw_http_listen(mgr, "http://127.0.0.1:0", sw_test_handler, &state) == 0);
    port = sw_mgr_get_listener_port(mgr, 0);
    assert(port != 0);

    response = issue_request(mgr, port, "GET /token-login HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "login id=") != NULL);
    assert(strstr(response.data, "user_id=user-1 role=admin") != NULL);
    response_body_field(&response, "id=", token_id, sizeof(token_id));
    response_cookie_value(&response, "tok", token_cookie, sizeof(token_cookie));
    assert(strlen(token_id) == 64);
    assert(strncmp(token_cookie, "v1.", 3) == 0);
    assert(strcmp(token_cookie, token_id) != 0);
    assert(strstr(token_cookie, token_id) == NULL);
    free(response.data);

    assert(snprintf(request, sizeof(request),
        "GET /token-current HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: tok=%s\r\n"
        "\r\n",
        token_cookie) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 200 OK") != NULL);
    assert(strstr(response.data, "current id=") != NULL);
    assert(strstr(response.data, token_id) != NULL);
    assert(strstr(response.data, "user_id=user-1 role=admin") != NULL);
    assert(strstr(response.data, "Set-Cookie: tok=v1.") != NULL);
    response_cookie_value(&response, "tok", refreshed_cookie, sizeof(refreshed_cookie));
    assert(strncmp(refreshed_cookie, "v1.", 3) == 0);
    free(response.data);

    assert(snprintf(request, sizeof(request),
        "GET /token-login HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: tok=%s\r\n"
        "\r\n",
        refreshed_cookie) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    response_cookie_value(&response, "tok", replacement_cookie, sizeof(replacement_cookie));
    assert(strncmp(replacement_cookie, "v1.", 3) == 0);
    free(response.data);

    assert(snprintf(request, sizeof(request),
        "GET /token-current HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: tok=%s\r\n"
        "\r\n",
        refreshed_cookie) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 401 Unauthorized") != NULL);
    assert(strstr(response.data, "Set-Cookie: tok=; Max-Age=0;") != NULL);
    free(response.data);

    assert(snprintf(request, sizeof(request),
        "GET /token-current HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: tok=%s\r\n"
        "\r\n",
        replacement_cookie) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 200 OK") != NULL);
    response_cookie_value(&response, "tok", refreshed_cookie, sizeof(refreshed_cookie));
    free(response.data);

    response = issue_request(mgr, port, "GET /token-items HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "first=0 second=0 third=-1 removed=0 third_after_remove=0") != NULL);
    free(response.data);

    response = issue_request(mgr, port, "GET /token-login HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    response_cookie_value(&response, "tok", second_cookie, sizeof(second_cookie));
    free(response.data);

    response = issue_request(mgr, port, "GET /token-login HTTP/1.1\r\nHost: localhost\r\n\r\n");
    assert_complete_response(&response);
    response_cookie_value(&response, "tok", third_cookie, sizeof(third_cookie));
    free(response.data);

    assert(snprintf(request, sizeof(request),
        "GET /token-logout HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: tok=%s\r\n"
        "\r\n",
        second_cookie) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    assert(strstr(response.data, "Set-Cookie: tok=; Max-Age=0; Expires=Thu, 01 Jan 1970 00:00:00 GMT; Path=/; HttpOnly; SameSite=Lax") != NULL);
    free(response.data);

    assert(snprintf(request, sizeof(request),
        "GET /token-current HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: tok=%s\r\n"
        "\r\n",
        second_cookie) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 401 Unauthorized") != NULL);
    assert(strstr(response.data, "Set-Cookie: tok=; Max-Age=0;") != NULL);
    free(response.data);

    assert(snprintf(request, sizeof(request),
        "GET /token-current HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: tok=%s\r\n"
        "\r\n",
        third_cookie) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 200 OK") != NULL);
    free(response.data);

    tamper_cookie_value(refreshed_cookie, bad_cookie, sizeof(bad_cookie));
    assert(snprintf(request, sizeof(request),
        "GET /token-current HTTP/1.1\r\nHost: localhost\r\nCookie: tok=%s\r\n\r\n",
        bad_cookie) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 401 Unauthorized") != NULL);
    assert(strstr(response.data, "Set-Cookie: tok=; Max-Age=0;") != NULL);
    free(response.data);

    strcpy(bad_cookie, refreshed_cookie);
    bad_cookie[10] = '\0';
    assert(snprintf(request, sizeof(request),
        "GET /token-current HTTP/1.1\r\nHost: localhost\r\nCookie: tok=%s\r\n\r\n",
        bad_cookie) > 0);
    response = issue_request(mgr, port, request);
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 401 Unauthorized") != NULL);
    assert(strstr(response.data, "Set-Cookie: tok=; Max-Age=0;") != NULL);
    free(response.data);

    response = issue_request(mgr, port,
        "GET /token-current HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: tok=v1.!!!!\r\n"
        "\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 401 Unauthorized") != NULL);
    assert(strstr(response.data, "Set-Cookie: tok=; Max-Age=0;") != NULL);
    free(response.data);

    response = issue_request(mgr, port,
        "GET /token-current HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Cookie: tok=v2.not-a-token\r\n"
        "\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 401 Unauthorized") != NULL);
    assert(strstr(response.data, "Set-Cookie: tok=; Max-Age=0;") != NULL);
    free(response.data);

    sw_mgr_destroy(mgr);
    sw_tokens_destroy(state.tokens);

    {
        sw_token_config expire_config = config;
        sw_mgr* expire_mgr = sw_mgr_create(NULL);
        sw_test_server_state expire_state = {0};
        u16 expire_port;

        expire_config.ttl_seconds = 1;
        expire_config.max_tokens = 4;
        expire_state.tokens = sw_tokens_create(&expire_config);
        assert(expire_mgr != NULL);
        assert(expire_state.tokens != NULL);
        assert(sw_http_listen(expire_mgr, "http://127.0.0.1:0", sw_test_handler, &expire_state) == 0);
        expire_port = sw_mgr_get_listener_port(expire_mgr, 0);
        assert(expire_port != 0);

        response = issue_request(expire_mgr, expire_port, "GET /token-login HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert_complete_response(&response);
        response_cookie_value(&response, "tok", token_cookie, sizeof(token_cookie));
        free(response.data);

        sw_test_sleep_ms(1100);
        assert(snprintf(request, sizeof(request),
            "GET /token-current HTTP/1.1\r\nHost: localhost\r\nCookie: tok=%s\r\n\r\n",
            token_cookie) > 0);
        response = issue_request(expire_mgr, expire_port, request);
        assert_complete_response(&response);
        assert(strstr(response.data, "HTTP/1.1 401 Unauthorized") != NULL);
        assert(strstr(response.data, "Set-Cookie: tok=; Max-Age=0;") != NULL);
        free(response.data);

        sw_mgr_destroy(expire_mgr);
        sw_tokens_destroy(expire_state.tokens);
    }

    {
        sw_token_config evict_config = config;
        sw_mgr* evict_mgr = sw_mgr_create(NULL);
        sw_test_server_state evict_state = {0};
        u16 evict_port;
        char first_cookie[256];

        evict_config.max_tokens = 2;
        evict_state.tokens = sw_tokens_create(&evict_config);
        assert(evict_mgr != NULL);
        assert(evict_state.tokens != NULL);
        assert(sw_http_listen(evict_mgr, "http://127.0.0.1:0", sw_test_handler, &evict_state) == 0);
        evict_port = sw_mgr_get_listener_port(evict_mgr, 0);
        assert(evict_port != 0);

        response = issue_request(evict_mgr, evict_port, "GET /token-login HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert_complete_response(&response);
        response_cookie_value(&response, "tok", first_cookie, sizeof(first_cookie));
        free(response.data);

        sw_test_sleep_ms(2);
        response = issue_request(evict_mgr, evict_port, "GET /token-login HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert_complete_response(&response);
        response_cookie_value(&response, "tok", second_cookie, sizeof(second_cookie));
        free(response.data);

        sw_test_sleep_ms(2);
        response = issue_request(evict_mgr, evict_port, "GET /token-login HTTP/1.1\r\nHost: localhost\r\n\r\n");
        assert_complete_response(&response);
        response_cookie_value(&response, "tok", third_cookie, sizeof(third_cookie));
        free(response.data);

        assert(snprintf(request, sizeof(request),
            "GET /token-current HTTP/1.1\r\nHost: localhost\r\nCookie: tok=%s\r\n\r\n",
            first_cookie) > 0);
        response = issue_request(evict_mgr, evict_port, request);
        assert_complete_response(&response);
        assert(strstr(response.data, "HTTP/1.1 401 Unauthorized") != NULL);
        assert(strstr(response.data, "Set-Cookie: tok=; Max-Age=0;") != NULL);
        free(response.data);

        assert(snprintf(request, sizeof(request),
            "GET /token-current HTTP/1.1\r\nHost: localhost\r\nCookie: tok=%s\r\n\r\n",
            second_cookie) > 0);
        response = issue_request(evict_mgr, evict_port, request);
        assert_complete_response(&response);
        assert(strstr(response.data, "HTTP/1.1 200 OK") != NULL);
        free(response.data);

        assert(snprintf(request, sizeof(request),
            "GET /token-current HTTP/1.1\r\nHost: localhost\r\nCookie: tok=%s\r\n\r\n",
            third_cookie) > 0);
        response = issue_request(evict_mgr, evict_port, request);
        assert_complete_response(&response);
        assert(strstr(response.data, "HTTP/1.1 200 OK") != NULL);
        free(response.data);

        sw_mgr_destroy(evict_mgr);
        sw_tokens_destroy(evict_state.tokens);
    }
#else
    assert(sw_tokens_create(NULL) == NULL);
#endif
}

static void test_server_config(void) {
    sw_server_config config = sw_server_config_default();
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

    response = issue_request(mgr, port,
        "POST /form HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "11\r\n"
        "12345678901234567\r\n"
        "0\r\n"
        "\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 413 Payload Too Large") != NULL);
    free(response.data);

    response = issue_request(mgr, port,
        "POST /form HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Transfer-Encoding: chunked\r\n"
        "\r\n"
        "z\r\n"
        "name=Jane+Doe\r\n"
        "0\r\n"
        "\r\n");
    assert_complete_response(&response);
    assert(strstr(response.data, "HTTP/1.1 400 Bad Request") != NULL);
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

static void test_server_keep_alive_and_limits(void) {
    sw_server_config config = sw_server_config_default();
    sw_mgr* mgr;
    sw_test_server_state state = {0};
    sw_test_socket fd;
    sw_test_socket rejected_fd;
    sw_test_response response;
    u16 port;

    config.keep_alive_max_requests = 2;
    config.max_connections = 1;
    config.idle_timeout_ms = 1000;

    mgr = sw_mgr_create(&config);
    assert(mgr != NULL);
    assert(sw_http_listen(mgr, "http://127.0.0.1:0", sw_test_handler, &state) == 0);
    port = sw_mgr_get_listener_port(mgr, 0);
    assert(port != 0);

    fd = connect_to_port(port);
    assert(send(fd,
        "GET / HTTP/1.1\r\nHost: localhost\r\n\r\n",
        (int)strlen("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        0) == (int)strlen("GET / HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    response = read_response_from_socket(mgr, fd, 80, 5, 1);
    assert_complete_response(&response);
    assert(strstr(response.data, "Connection: keep-alive") != NULL);
    assert(sw_server_open_connections(mgr) == 1);
    free(response.data);

    rejected_fd = connect_to_port(port);
    assert(wait_socket_closed(mgr, rejected_fd, 80, 5, 1));
#ifdef _WIN32
    closesocket(rejected_fd);
#else
    close(rejected_fd);
#endif

    assert(send(fd,
        "GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n",
        (int)strlen("GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n"),
        0) == (int)strlen("GET /style.css HTTP/1.1\r\nHost: localhost\r\n\r\n"));
    response = read_response_from_socket(mgr, fd, 80, 5, 1);
    assert_complete_response(&response);
    assert(strstr(response.data, "Connection: close") != NULL);
    free(response.data);
    assert(wait_socket_closed(mgr, fd, 80, 5, 1));
#ifdef _WIN32
    closesocket(fd);
#else
    close(fd);
#endif

    sw_mgr_destroy(mgr);
}

static void test_server_loop_shards(void) {
    sw_server_config config = sw_server_config_default();
    sw_server* server;
    sw_test_server_state state = {0};
    sw_test_socket sockets[4];
    u16 port;
    sz i;

    config.worker_count = 2;
    config.idle_timeout_ms = 1000;

    server = sw_server_create(&config);
    assert(server != NULL);
    assert(sw_server_add_http(server, "http://127.0.0.1:0", sw_test_handler, &state) == 0);
    port = sw_server_get_listener_port(server, 0);
    assert(port != 0);
    assert(sw_server_start(server) == 0);

    for (i = 0; i < 4; ++i) {
        sw_test_response response;
        sockets[i] = connect_to_port(port);
        assert(send(sockets[i],
            "GET /style.css HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
            (int)strlen("GET /style.css HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"),
            0) == (int)strlen("GET /style.css HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n"));
        response = read_response_background(sockets[i], 200, 2);
        assert_complete_response(&response);
        assert(strstr(response.data, "body { color: #fff; }") != NULL);
        free(response.data);
#ifdef _WIN32
        closesocket(sockets[i]);
#else
        close(sockets[i]);
#endif
    }

    sw_server_stop(server);
    assert(sw_server_wait(server) == 0);
    assert(sw_server_worker_count(server) == 2);
    assert(sw_server_open_connections(server) == 0);
    assert(sw_server_worker_accepted_connections(server, 0) > 0);
    assert(sw_server_worker_accepted_connections(server, 1) > 0);
    assert(sw_server_worker_open_connections(server, 0) == 0);
    assert(sw_server_worker_open_connections(server, 1) == 0);
    sw_server_destroy(server);
}

static void test_database_helpers(void) {
#if !defined(SYPHAX_WEB_HAS_SQLITE) && !defined(SYPHAX_WEB_HAS_POSTGRES)
    {
        sw_db* db = sw_db_open(NULL);
        assert(db == NULL);
        assert(sw_db_error(NULL) != NULL);
        assert(sw_db_error(NULL)[0] != '\0');
    }
#endif

#if defined(SYPHAX_WEB_HAS_SQLITE)
    {
        const u8 payload[] = { 1, 2, 3, 0 };
        const void* blob = NULL;
        sw_db* db = sw_db_open(NULL);
        sw_db_stmt* stmt;

        assert(db != NULL);
        assert(sw_db_get_driver(db) == SW_DB_DRIVER_SQLITE);
        assert(strcmp(sw_db_driver_name(sw_db_get_driver(db)), "sqlite") == 0);

        assert(sw_db_exec(db,
            "CREATE TABLE notes ("
            "id INTEGER PRIMARY KEY AUTOINCREMENT,"
            "title TEXT NOT NULL,"
            "hits INTEGER NOT NULL,"
            "rating REAL NOT NULL,"
            "payload BLOB"
            ")") == 0);

        stmt = sw_db_prepare(db, "INSERT INTO notes(title, hits, rating, payload) VALUES(?, ?, ?, ?)");
        assert(stmt != NULL);
        assert(sw_db_bind_text(stmt, 1, "hello") == 0);
        assert(sw_db_bind_int(stmt, 2, 7) == 0);
        assert(sw_db_bind_float(stmt, 3, 3.25) == 0);
        assert(sw_db_bind_blob(stmt, 4, payload, sizeof(payload)) == 0);
        assert(sw_db_step(stmt) == SW_DB_DONE);
        sw_db_finalize(stmt);
        assert(sw_db_changes(db) == 1);
        assert(sw_db_last_insert_id(db) == 1);

        stmt = sw_db_prepare(db, "SELECT '?' AS literal, id, title, hits, rating, payload FROM notes WHERE title = ?");
        assert(stmt != NULL);
        assert(sw_db_bind_text(stmt, 1, "hello") == 0);
        assert(sw_db_step(stmt) == SW_DB_ROW);
        assert(sw_db_column_count(stmt) == 6);
        assert(strcmp(sw_db_column_name(stmt, 2), "title") == 0);
        assert(strcmp(sw_db_column_text(stmt, 0), "?") == 0);
        assert(sw_db_column_type(stmt, 2) == SW_DB_VALUE_TEXT);
        assert(strcmp(sw_db_column_text(stmt, 2), "hello") == 0);
        assert(sw_db_column_int(stmt, 3) == 7);
        assert(sw_db_column_float(stmt, 4) > 3.2);
        assert(sw_db_column_float(stmt, 4) < 3.3);
        assert(sw_db_column_type(stmt, 5) == SW_DB_VALUE_BLOB);
        assert(sw_db_column_blob(stmt, 5, &blob) == sizeof(payload));
        assert(blob != NULL);
        assert(memcmp(blob, payload, sizeof(payload)) == 0);
        assert(sw_db_step(stmt) == SW_DB_DONE);
        sw_db_finalize(stmt);

        stmt = sw_db_prepare(db, "SELECT title FROM notes WHERE hits = ?");
        assert(stmt != NULL);
        assert(sw_db_bind_int(stmt, 1, 7) == 0);
        assert(sw_db_step(stmt) == SW_DB_ROW);
        assert(strcmp(sw_db_column_text(stmt, 0), "hello") == 0);
        assert(sw_db_reset(stmt) == 0);
        assert(sw_db_bind_int(stmt, 1, 99) == 0);
        assert(sw_db_step(stmt) == SW_DB_DONE);
        sw_db_finalize(stmt);

        assert(sw_db_begin(db) == 0);
        assert(sw_db_exec(db, "INSERT INTO notes(title, hits, rating, payload) VALUES('rollback', 1, 1.0, NULL)") == 0);
        assert(sw_db_rollback(db) == 0);

        stmt = sw_db_prepare(db, "SELECT COUNT(*) FROM notes");
        assert(stmt != NULL);
        assert(sw_db_step(stmt) == SW_DB_ROW);
        assert(sw_db_column_int(stmt, 0) == 1);
        sw_db_finalize(stmt);

        assert(sw_db_exec(db, "SELECT * FROM missing_table") != 0);
        assert(sw_db_error(db) != NULL);
        assert(sw_db_error(db)[0] != '\0');

        sw_db_close(db);
    }
#endif

#if defined(SYPHAX_WEB_HAS_POSTGRES)
    {
        const c8* url = getenv("SYPHAX_WEB_TEST_POSTGRES_URL");

        if (url != NULL && url[0] != '\0') {
            const u8 payload[] = { 9, 8, 7, 0 };
            const void* blob = NULL;
            sw_db_config config = sw_db_config_default();
            sw_db* db;
            sw_db_stmt* stmt;

            config.url = url;
            db = sw_db_open(&config);
            assert(db != NULL);
            assert(sw_db_get_driver(db) == SW_DB_DRIVER_POSTGRES);
            assert(strcmp(sw_db_driver_name(sw_db_get_driver(db)), "postgres") == 0);

            assert(sw_db_exec(db, "CREATE TEMP TABLE sw_db_test (title TEXT NOT NULL, hits BIGINT NOT NULL, payload BYTEA)") == 0);

            stmt = sw_db_prepare(db, "INSERT INTO sw_db_test(title, hits, payload) VALUES(?, ?, ?)");
            assert(stmt != NULL);
            assert(sw_db_bind_text(stmt, 1, "hello") == 0);
            assert(sw_db_bind_int(stmt, 2, 12) == 0);
            assert(sw_db_bind_blob(stmt, 3, payload, sizeof(payload)) == 0);
            assert(sw_db_step(stmt) == SW_DB_DONE);
            sw_db_finalize(stmt);
            assert(sw_db_changes(db) == 1);

            stmt = sw_db_prepare(db, "SELECT '?' AS literal, title, hits, payload FROM sw_db_test WHERE hits = ?");
            assert(stmt != NULL);
            assert(sw_db_bind_int(stmt, 1, 12) == 0);
            assert(sw_db_step(stmt) == SW_DB_ROW);
            assert(sw_db_column_count(stmt) == 4);
            assert(strcmp(sw_db_column_text(stmt, 0), "?") == 0);
            assert(strcmp(sw_db_column_text(stmt, 1), "hello") == 0);
            assert(sw_db_column_int(stmt, 2) == 12);
            assert(sw_db_column_type(stmt, 3) == SW_DB_VALUE_BLOB);
            assert(sw_db_column_blob(stmt, 3, &blob) == sizeof(payload));
            assert(blob != NULL);
            assert(memcmp(blob, payload, sizeof(payload)) == 0);
            assert(sw_db_step(stmt) == SW_DB_DONE);
            sw_db_finalize(stmt);

            stmt = sw_db_prepare(db, "SELECT E'quote\\'?literal' AS literal, ?::BIGINT");
            assert(stmt != NULL);
            assert(sw_db_bind_int(stmt, 1, 33) == 0);
            assert(sw_db_step(stmt) == SW_DB_ROW);
            assert(strcmp(sw_db_column_text(stmt, 0), "quote'?literal") == 0);
            assert(sw_db_column_int(stmt, 1) == 33);
            assert(sw_db_step(stmt) == SW_DB_DONE);
            sw_db_finalize(stmt);

            assert(sw_db_begin(db) == 0);
            assert(sw_db_exec(db, "INSERT INTO sw_db_test(title, hits, payload) VALUES('rollback', 1, NULL)") == 0);
            assert(sw_db_rollback(db) == 0);

            stmt = sw_db_prepare(db, "SELECT COUNT(*) FROM sw_db_test");
            assert(stmt != NULL);
            assert(sw_db_step(stmt) == SW_DB_ROW);
            assert(sw_db_column_int(stmt, 0) == 1);
            sw_db_finalize(stmt);

            sw_db_close(db);
        }
    }
#endif
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

        response = issue_tls_request(mgr, port,
            "GET /cookie HTTP/1.1\r\n"
            "Host: localhost\r\n"
            "\r\n",
            NULL,
            0,
            NULL,
            0);
        assert(sw_test_response_has_complete_body(&response));
        assert(strstr(response.data, "Set-Cookie: tls_cookie=1; Path=/; Secure; HttpOnly; SameSite=Lax") != NULL);
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
    { "response_helpers", test_response_helpers },
    { "large_multipart_upload", test_large_multipart_upload },
    { "large_multipart_upload_failure", test_large_multipart_upload_failure_keeps_path },
    { "cookie_helpers", test_cookie_helpers },
    { "session_helpers", test_session_helpers },
    { "token_helpers", test_token_helpers },
    { "server_config", test_server_config },
    { "server_keep_alive_and_limits", test_server_keep_alive_and_limits },
    { "server_loop_shards", test_server_loop_shards },
    { "database_helpers", test_database_helpers },
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
