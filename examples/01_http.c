#include "example_common.h"

static void render_home(sw_connection* connection) {
    const b8 secure = sw_connection_is_secure(connection);
    sw_buffer* h = sw_buffer_new();

    sw_html(h, sw_attrs(sw_attr("lang", "en")), {
        sw_head(h, sw_attrs(), {
            render_head(h, secure ? "Syphax-Web HTTPS" : "Syphax-Web HTTP");
        });
        sw_body(h, sw_attrs(sw_attr("class", "page")), {
            sw_main(h, sw_attrs(sw_attr("class", "shell")), {
                sw_section(h, sw_attrs(sw_attr("class", "hero")), {
                    sw_span(h, sw_attrs(sw_attr("class", secure ? "kicker secure" : "kicker")), {
                        sw_text(h, secure ? "HTTPS" : "HTTP");
                    });
                    sw_h1(h, sw_attrs(), {
                        sw_text(h, secure ? "Small HTTPS server" : "Small HTTP server");
                    });
                    sw_p(h, sw_attrs(sw_attr("class", "lead")), {
                        sw_text(h, "A minimal handler renders HTML, serves a shared stylesheet, and returns a plain health response.");
                    });
                    sw_div(h, sw_attrs(sw_attr("class", "actions")), {
                        sw_a(h, sw_attrs(sw_attr("class", "button"), sw_attr("href", "/health")), {
                            sw_text(h, "Health");
                        });
                    });
                });

                sw_section(h, sw_attrs(sw_attr("class", "grid three")), {
                    sw_article(h, sw_attrs(sw_attr("class", "card")), {
                        sw_h2(h, sw_attrs(), { sw_text(h, "Route"); });
                        sw_p(h, sw_attrs(), { sw_text(h, "GET / renders this page."); });
                    });
                    sw_article(h, sw_attrs(sw_attr("class", "card")), {
                        sw_h2(h, sw_attrs(), { sw_text(h, "Asset"); });
                        sw_p(h, sw_attrs(), { sw_text(h, "GET /style.css is served from examples/shared."); });
                    });
                    sw_article(h, sw_attrs(sw_attr("class", "card")), {
                        sw_h2(h, sw_attrs(), { sw_text(h, "Status"); });
                        sw_p(h, sw_attrs(), { sw_text(h, "GET /health returns text/plain."); });
                    });
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
    if (sw_http_is(request, "GET", "/health")) {
        (void)sw_http_replyf(connection, 200, "text/plain; charset=utf-8", "ok\n");
        return;
    }

    reply_not_found(connection);
}

int main(void) {
    sw_server_config config = server_config();

#if defined(SYPHAX_WEB_HAS_TLS)
    return listen_https(
        EXAMPLE_HTTPS_URL,
        &config,
        handle_request,
        NULL,
        "Syphax-Web basic HTTPS example"
    );
#else
    printf("Syphax-Web HTTP example\n");
    printf("Open %s in your browser\n", EXAMPLE_HTTP_URL);
    return sw_server_listen(EXAMPLE_HTTP_URL, &config, handle_request, NULL);
#endif
}
