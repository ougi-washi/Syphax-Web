#include "example_common.h"

static void render_home(sw_connection* connection) {
    const c8* alpn = sw_connection_alpn(connection);
    const c8* remote_ip = sw_connection_remote_ip(connection);
    b8 local_cert = using_local_cert();
    sw_buffer* h = sw_buffer_new();

    sw_html(h, sw_attrs(sw_attr("lang", "en")), {
        sw_head(h, sw_attrs(), {
            render_head(h, "Syphax-Web HTTPS");
        });
        sw_body(h, sw_attrs(sw_attr("class", "page")), {
            sw_main(h, sw_attrs(sw_attr("class", "shell")), {
                sw_section(h, sw_attrs(sw_attr("class", "hero")), {
                    sw_span(h, sw_attrs(sw_attr("class", "kicker secure")), {
                        sw_text(h, "TLS");
                    });
                    sw_h1(h, sw_attrs(), {
                        sw_text(h, "HTTPS listener");
                    });
                    sw_p(h, sw_attrs(sw_attr("class", "lead")), {
                        sw_text(h, "This server is using the native OpenSSL transport and the normal HTTP/1.1 handler API.");
                    });
                });

                sw_section(h, sw_attrs(sw_attr("class", "status-grid")), {
                    sw_div(h, sw_attrs(sw_attr("class", "metric")), {
                        sw_span(h, sw_attrs(), { sw_text(h, "Secure"); });
                        sw_strong(h, sw_attrs(), { sw_text(h, sw_connection_is_secure(connection) ? "yes" : "no"); });
                    });
                    sw_div(h, sw_attrs(sw_attr("class", "metric")), {
                        sw_span(h, sw_attrs(), { sw_text(h, "ALPN"); });
                        sw_strong(h, sw_attrs(), { sw_text(h, (alpn != NULL && alpn[0] != '\0') ? alpn : "none"); });
                    });
                    sw_div(h, sw_attrs(sw_attr("class", "metric")), {
                        sw_span(h, sw_attrs(), { sw_text(h, "Remote"); });
                        sw_strong(h, sw_attrs(), { sw_text(h, (remote_ip != NULL) ? remote_ip : "unknown"); });
                    });
                });

                sw_div(h, sw_attrs(sw_attr("class", "notice")), {
                    sw_text(h, local_cert
                        ? "This example is using the local mkcert certificate. Restart the browser if it still shows an old certificate warning."
                        : "This example is using the certificate configured through SYPHAX_WEB_TLS_CERT and SYPHAX_WEB_TLS_KEY.");
                });
            });
        });
    });

    (void)sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
    sw_buffer_free(h);
}

static void handle_request(sw_connection* connection, const sw_http_message* request, void* user_data) {
    (void)user_data;

    if (serve_style(connection, request)) {
        return;
    }
    if (sw_http_is(request, "GET", "/")) {
        render_home(connection);
        return;
    }

    reply_not_found(connection);
}

int main(void) {
    sw_http_config config = http_config();

    return listen_https(
        "https://127.0.0.1:8443",
        &config,
        handle_request,
        NULL,
        "Syphax-Web HTTPS example"
    );
}
