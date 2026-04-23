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

static void render_nav(sw_buffer* h) {
    sw_nav(h, sw_attrs(sw_attr("class", "nav")), {
        sw_a(h, sw_attrs(sw_attr("href", "/")), { sw_text(h, "Overview"); });
        sw_a(h, sw_attrs(sw_attr("href", "/report")), { sw_text(h, "Report"); });
        sw_a(h, sw_attrs(sw_attr("href", "/status.json")), { sw_text(h, "JSON"); });
        sw_a(h, sw_attrs(sw_attr("href", "/about.txt")), { sw_text(h, "Text"); });
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

static void render_page(sw_connection* connection, const c8* title, b8 report_page) {
    sz i;
    sw_buffer* h = sw_buffer_new();

    sw_html(h, sw_attrs(sw_attr("lang", "en")), {
        sw_head(h, sw_attrs(), {
            render_head(h, title);
        });
        sw_body(h, sw_attrs(sw_attr("class", "page")), {
            sw_main(h, sw_attrs(sw_attr("class", "shell wide")), {
                render_nav(h);
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
    (void)user_data;

    if (serve_style(connection, request)) {
        return;
    }
    if (sw_http_is(request, "GET", "/")) {
        render_page(connection, "Static Site", 0);
        return;
    }
    if (sw_http_is(request, "GET", "/report")) {
        render_page(connection, "Static Report", 1);
        return;
    }
    if (sw_http_is(request, "GET", "/status.json")) {
        (void)sw_http_replyf(connection, 200, "application/json; charset=utf-8",
            "{\"secure\":true,\"mode\":\"static\",\"routes\":5}\n");
        return;
    }
    if (sw_http_is(request, "GET", "/about.txt")) {
        (void)sw_http_replyf(connection, 200, "text/plain; charset=utf-8",
            "Static HTTPS example served by syphax-web.\n");
        return;
    }

    reply_not_found(connection);
}

int main(void) {
    sw_http_config config = http_config();

    return listen_https(
        "https://127.0.0.1:8444",
        &config,
        handle_request,
        NULL,
        "Syphax-Web complex static example"
    );
}
