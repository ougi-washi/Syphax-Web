#ifndef SW_EXAMPLE_COMMON_H
#define SW_EXAMPLE_COMMON_H

#include "sw_html.h"
#include "sw_server.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef SYPHAX_WEB_SOURCE_DIR
#    define SYPHAX_WEB_SOURCE_DIR "."
#endif

#define COUNT_OF(_items) (sizeof(_items) / sizeof((_items)[0]))

static inline void repo_path(c8* buffer, sz buffer_len, const c8* relative_path) {
    if (buffer == NULL || buffer_len == 0) {
        return;
    }
    (void)snprintf(buffer, buffer_len, "%s/%s", SYPHAX_WEB_SOURCE_DIR, relative_path);
}

static inline void shared_root(c8* buffer, sz buffer_len) {
    repo_path(buffer, buffer_len, "examples/shared");
}

static inline void advanced_root(c8* buffer, sz buffer_len) {
    repo_path(buffer, buffer_len, "examples/advanced");
}

static inline b8 file_exists(const c8* path) {
    FILE* file;

    if (path == NULL || path[0] == '\0') {
        return 0;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }

    fclose(file);
    return 1;
}

static inline void render_head(sw_buffer* h, const c8* title) {
    sw_meta_charset(h, "utf-8");
    sw_meta(h, sw_attrs(
        sw_attr("name", "viewport"),
        sw_attr("content", "width=device-width, initial-scale=1")
    ));
    sw_title(h, title);
    sw_link(h, sw_attrs(
        sw_attr("rel", "stylesheet"),
        sw_attr("href", "/style.css")
    ));
}

static inline sw_http_config http_config(void) {
    sw_http_config config = sw_http_config_default();

    config.max_body_bytes = 256 * 1024;
    config.idle_timeout_ms = 20000;
    return config;
}

static inline sw_tls_config tls_config(void) {
    static c8 local_certificate_path[1024];
    static c8 local_private_key_path[1024];
    const c8* certificate_env = getenv("SYPHAX_WEB_TLS_CERT");
    const c8* private_key_env = getenv("SYPHAX_WEB_TLS_KEY");
    sw_tls_config tls = sw_tls_config_default();

    if (certificate_env != NULL && certificate_env[0] != '\0'
        && private_key_env != NULL && private_key_env[0] != '\0') {
        tls.certificate_file = certificate_env;
        tls.private_key_file = private_key_env;
        return tls;
    }

    repo_path(local_certificate_path, sizeof(local_certificate_path), "examples/shared/localhost.local.crt");
    repo_path(local_private_key_path, sizeof(local_private_key_path), "examples/shared/localhost.local.key");
    if (file_exists(local_certificate_path) && file_exists(local_private_key_path)) {
        tls.certificate_file = local_certificate_path;
        tls.private_key_file = local_private_key_path;
        return tls;
    }

    return tls;
}

static inline b8 using_local_cert(void) {
    c8 local_certificate_path[1024];
    c8 local_private_key_path[1024];
    const c8* certificate_env = getenv("SYPHAX_WEB_TLS_CERT");
    const c8* private_key_env = getenv("SYPHAX_WEB_TLS_KEY");

    if (certificate_env != NULL && certificate_env[0] != '\0'
        && private_key_env != NULL && private_key_env[0] != '\0') {
        return 0;
    }

    repo_path(local_certificate_path, sizeof(local_certificate_path), "examples/shared/localhost.local.crt");
    repo_path(local_private_key_path, sizeof(local_private_key_path), "examples/shared/localhost.local.key");
    return file_exists(local_certificate_path) && file_exists(local_private_key_path);
}

static inline b8 has_tls_files(const sw_tls_config* tls) {
    return tls != NULL
        && file_exists(tls->certificate_file)
        && file_exists(tls->private_key_file);
}

static inline b8 serve_style(sw_connection* connection, const sw_http_message* request) {
    c8 root[1024];

    if (!sw_http_is(request, "GET", "/style.css")) {
        return 0;
    }

    shared_root(root, sizeof(root));
    (void)sw_http_serve_path(connection, root, "/style.css");
    return 1;
}

static inline void reply_not_found(sw_connection* connection) {
    (void)sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "Not Found");
}

static inline void copy_text(c8* out, sz out_len, const c8* text) {
    if (out == NULL || out_len == 0) {
        return;
    }
    (void)snprintf(out, out_len, "%s", (text != NULL) ? text : "");
}

static inline i32 listen_https(
    const c8* url,
    const sw_http_config* http,
    sw_http_handler handler,
    void* user_data,
    const c8* label
) {
#if defined(SYPHAX_WEB_HAS_TLS)
    sw_tls_config tls = tls_config();

    printf("%s\n", (label != NULL) ? label : "Syphax-Web HTTPS example");
    if (!has_tls_files(&tls)) {
        fprintf(stderr, "Missing TLS certificate/key.\n");
        fprintf(stderr, "Generate local files with:\n");
        fprintf(stderr, "  mkcert -cert-file examples/shared/localhost.local.crt -key-file examples/shared/localhost.local.key localhost 127.0.0.1 ::1\n");
        fprintf(stderr, "Or set SYPHAX_WEB_TLS_CERT and SYPHAX_WEB_TLS_KEY.\n");
        return 1;
    }

    printf("Using certificate: %s\n", tls.certificate_file);
    if (using_local_cert()) {
        printf("If the browser still warns, install the mkcert CA into its trust store and restart the browser.\n");
    }
    printf("Open %s in your browser\n", url);
    return sw_server_listen_tls(url, http, &tls, handler, user_data);
#else
    (void)url;
    (void)http;
    (void)handler;
    (void)user_data;
    (void)label;
    fprintf(stderr, "This example requires a TLS build. Run ./build.sh -tls.\n");
    return 1;
#endif
}

#endif
