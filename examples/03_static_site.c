#include "example_common.h"

typedef struct {
    const c8* label;
    const c8* value;
    const c8* note;
} metric;

typedef struct {
    const c8* title;
    const c8* body;
} card;

static const metric metrics[] = {
    { "Routes", "5", "HTML, JSON, text, and CSS responses" },
    { "Mode", "TLS", "OpenSSL-backed HTTPS listener" },
    { "State", "Static", "No mutable server data" }
};

static const card cards[] = {
    { "Landing Page", "Server-rendered layout with shared assets and simple navigation." },
    { "Operations Report", "Static tables and cards show dense information without client state." },
    { "Asset Boundary", "Public files are served through sw_http_serve_path with a fixed docroot." }
};

static void render_nav_link(sw_buffer* h, const c8* path, const c8* label, const c8* lang) {
    c8 href[128];

    language_url(href, sizeof(href), path, lang);
    sw_a(h, sw_attrs(sw_attr("href", href)), { sw_text(h, label); });
}

static void render_nav(sw_buffer* h, const c8* current_path, const c8* lang) {
    sw_nav(h, sw_attrs(sw_attr("class", "nav")), {
        render_nav_link(h, "/", "Overview", lang);
        render_nav_link(h, "/report", "Report", lang);
        render_nav_link(h, "/status.json", "JSON", lang);
        render_nav_link(h, "/about.txt", "Text", lang);
        render_language_links(h, current_path, lang);
    });
}

static void render_metrics(sw_buffer* h) {
    sz i;

    sw_section(h, sw_attrs(sw_attr("class", "status-grid")), {
        for (i = 0; i < COUNT_OF(metrics); ++i) {
            sw_div(h, sw_attrs(sw_attr("class", "metric")), {
                sw_span(h, sw_attrs(), { sw_text(h, metrics[i].label); });
                sw_strong(h, sw_attrs(), { sw_text(h, metrics[i].value); });
                sw_small(h, sw_attrs(), { sw_text(h, metrics[i].note); });
            });
        }
    });
}

static void render_page(
    sw_connection* connection,
    sw_translator* translator,
    const c8* title,
    const c8* path,
    b8 report_page
) {
    sz i;
    const c8* lang = current_language_code(translator);
    sw_buffer* h = sw_buffer_new();

    sw_buffer_set_translator(h, translator);
    sw_html(h, sw_attrs(), {
        sw_head(h, sw_attrs(), {
            render_head(h, title);
        });
        sw_body(h, sw_attrs(sw_attr("class", "page")), {
            sw_main(h, sw_attrs(sw_attr("class", "shell wide")), {
                render_nav(h, path, lang);
                sw_section(h, sw_attrs(sw_attr("class", "hero compact")), {
                    sw_span(h, sw_attrs(sw_attr("class", "kicker secure")), { sw_text(h, "Static HTTPS"); });
                    sw_h1(h, sw_attrs(), { sw_text(h, title); });
                    sw_p(h, sw_attrs(sw_attr("class", "lead")), {
                        sw_text(h, "A richer read-only site served by one C handler and the same shared stylesheet.");
                    });
                });

                render_metrics(h);

                if (report_page) {
                    sw_section(h, sw_attrs(sw_attr("class", "panel")), {
                        sw_h2(h, sw_attrs(), { sw_text(h, "Operations Report"); });
                        sw_div(h, sw_attrs(sw_attr("class", "table")), {
                            sw_div(h, sw_attrs(sw_attr("class", "table-row table-head")), {
                                sw_span(h, sw_attrs(), { sw_text(h, "Area"); });
                                sw_span(h, sw_attrs(), { sw_text(h, "Signal"); });
                                sw_span(h, sw_attrs(), { sw_text(h, "Owner"); });
                            });
                            sw_div(h, sw_attrs(sw_attr("class", "table-row")), {
                                sw_span(h, sw_attrs(), { sw_text(h, "Routing"); });
                                sw_span(h, sw_attrs(), { sw_text(h, "Healthy"); });
                                sw_span(h, sw_attrs(), { sw_text(h, "Core"); });
                            });
                            sw_div(h, sw_attrs(sw_attr("class", "table-row")), {
                                sw_span(h, sw_attrs(), { sw_text(h, "Assets"); });
                                sw_span(h, sw_attrs(), { sw_text(h, "Scoped"); });
                                sw_span(h, sw_attrs(), { sw_text(h, "Web"); });
                            });
                            sw_div(h, sw_attrs(sw_attr("class", "table-row")), {
                                sw_span(h, sw_attrs(), { sw_text(h, "TLS"); });
                                sw_span(h, sw_attrs(), { sw_text(h, "HTTP/1.1 ALPN"); });
                                sw_span(h, sw_attrs(), { sw_text(h, "Transport"); });
                            });
                        });
                    });
                } else {
                    sw_section(h, sw_attrs(sw_attr("class", "grid three")), {
                        for (i = 0; i < COUNT_OF(cards); ++i) {
                            sw_article(h, sw_attrs(sw_attr("class", "card")), {
                                sw_h2(h, sw_attrs(), { sw_text(h, cards[i].title); });
                                sw_p(h, sw_attrs(), { sw_text(h, cards[i].body); });
                            });
                        }
                    });
                }
            });
        });
    });

    (void)sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
    sw_buffer_free(h);
}

static void handle_request(sw_connection* connection, const sw_http_message* request, void* user_data) {
    sw_translator* translator = (sw_translator*)user_data;

    if (serve_style(connection, request)) {
        return;
    }
    if (sw_http_is(request, "GET", "/")) {
        use_query_language(translator, request);
        render_page(connection, translator, "Static Site", "/", 0);
        return;
    }
    if (sw_http_is(request, "GET", "/report")) {
        use_query_language(translator, request);
        render_page(connection, translator, "Static Report", "/report", 1);
        return;
    }
    if (sw_http_is(request, "GET", "/status.json")) {
        (void)sw_http_replyf(connection, 200, "application/json; charset=utf-8",
            "{\"secure\":true,\"mode\":\"static\",\"routes\":5}\n");
        return;
    }
    if (sw_http_is(request, "GET", "/about.txt")) {
        use_query_language(translator, request);
        (void)sw_http_replyf(connection, 200, "text/plain; charset=utf-8",
            "%s\n",
            sw_translate(translator, "Static HTTPS example served by syphax-web."));
        return;
    }

    reply_not_found(connection);
}

int main(void) {
    sw_server_config config = server_config();
    sw_translator* translator;
    i32 rc;

    translator = example_translator();
    if (translator == NULL) {
        fprintf(stderr, "Failed to load translations.\n");
        return 1;
    }

    rc = listen_https(
        EXAMPLE_HTTPS_URL,
        &config,
        handle_request,
        translator,
        "Syphax-Web static site example"
    );
    sw_translator_destroy(translator);
    return rc;
}
