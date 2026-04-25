#include "example_common.h"
#include "sw_js.h"
#include "sw_utility.h"

typedef struct {
    c8 name[80];
    c8 owner[48];
    c8 severity[24];
} work_item;

typedef struct {
    sw_translator* translator;
    work_item items[16];
    sz item_count;
} app_state;

static void seed_items(app_state* state) {
    if (state == NULL) {
        return;
    }

    state->item_count = 3;
    copy_text(state->items[0].name, sizeof(state->items[0].name), "Ship TLS rollout guide");
    copy_text(state->items[0].owner, sizeof(state->items[0].owner), "Core");
    copy_text(state->items[0].severity, sizeof(state->items[0].severity), "High");
    copy_text(state->items[1].name, sizeof(state->items[1].name), "Review static file paths");
    copy_text(state->items[1].owner, sizeof(state->items[1].owner), "Web");
    copy_text(state->items[1].severity, sizeof(state->items[1].severity), "Medium");
    copy_text(state->items[2].name, sizeof(state->items[2].name), "Refresh dashboard copy");
    copy_text(state->items[2].owner, sizeof(state->items[2].owner), "Docs");
    copy_text(state->items[2].severity, sizeof(state->items[2].severity), "Low");
}

static b8 field_matches(const sw_translator* translator, const c8* value, const c8* query) {
    const c8* translated = sw_translate(translator, value);

    return sw_matches_query(value, query, 0)
        || (translated != value && sw_matches_query(translated, query, 0));
}

static b8 item_matches(const work_item* item, const c8* query, const sw_translator* translator) {
    if (query == NULL || query[0] == '\0') {
        return 1;
    }
    return field_matches(translator, item->name, query)
        || field_matches(translator, item->owner, query)
        || field_matches(translator, item->severity, query);
}

static void render_items(sw_buffer* h, const app_state* state, const c8* query, const c8* message) {
    sz i;
    sz visible_count = 0;

    sw_div(h, sw_attrs(sw_attr("class", "panel")), {
        sw_div(h, sw_attrs(sw_attr("class", "panel-title")), {
            sw_h2(h, sw_attrs(), { sw_text(h, "Queue"); });
            if (message != NULL && message[0] != '\0') {
                sw_span(h, sw_attrs(sw_attr("class", "pill success")), { sw_text(h, message); });
            }
        });

        sw_div(h, sw_attrs(sw_attr("class", "table")), {
            sw_div(h, sw_attrs(sw_attr("class", "table-row table-head")), {
                sw_span(h, sw_attrs(), { sw_text(h, "Work"); });
                sw_span(h, sw_attrs(), { sw_text(h, "Owner"); });
                sw_span(h, sw_attrs(), { sw_text(h, "Priority"); });
            });
            for (i = 0; state != NULL && i < state->item_count; ++i) {
                const work_item* item = &state->items[i];

                if (!item_matches(item, query, state->translator)) {
                    continue;
                }
                visible_count += 1;
                sw_div(h, sw_attrs(sw_attr("class", "table-row")), {
                    sw_span(h, sw_attrs(), { sw_text(h, item->name); });
                    sw_span(h, sw_attrs(), { sw_text(h, item->owner); });
                    sw_span(h, sw_attrs(sw_attr("class", "pill")), { sw_text(h, item->severity); });
                });
            }
        });

        if (visible_count == 0) {
            sw_div(h, sw_attrs(sw_attr("class", "empty-state")), {
                sw_text(h, "No matching work items.");
            });
        }
    });
}

static void add_item(app_state* state, const sw_http_message* request, c8* message, sz message_len) {
    c8 name[80];
    c8 owner[48];
    c8 severity[24];
    work_item* item;

    if (state == NULL || message == NULL || message_len == 0) {
        return;
    }

    (void)sw_http_get_form(request, "name", name, sizeof(name));
    (void)sw_http_get_form(request, "owner", owner, sizeof(owner));
    (void)sw_http_get_form(request, "severity", severity, sizeof(severity));

    if (name[0] == '\0' || owner[0] == '\0') {
        copy_text(message, message_len, "Name and owner required");
        return;
    }
    if (state->item_count >= COUNT_OF(state->items)) {
        copy_text(message, message_len, "Queue full");
        return;
    }

    item = &state->items[state->item_count++];
    copy_text(item->name, sizeof(item->name), name);
    copy_text(item->owner, sizeof(item->owner), owner);
    copy_text(item->severity, sizeof(item->severity), (severity[0] != '\0') ? severity : "Medium");
    copy_text(message, message_len, "Added");
}

static void reply_items(sw_connection* connection, const app_state* state, const c8* query, const c8* message) {
    sw_buffer* h = sw_buffer_new();

    sw_buffer_set_translator(h, state != NULL ? state->translator : NULL);
    render_items(h, state, query, message);
    (void)sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
    sw_buffer_free(h);
}

static void render_home(sw_connection* connection, const app_state* state, const sw_http_message* request) {
    const c8* lang = current_language_code(state != NULL ? state->translator : NULL);
    c8 seen[8];
    sw_buffer* h = sw_buffer_new();

    (void)sw_http_get_cookie(request, "sw_queue_seen", seen, sizeof(seen));
    (void)sw_http_set_cookie(connection, "sw_queue_seen", "1", NULL);

    sw_buffer_set_translator(h, state != NULL ? state->translator : NULL);
    sw_html(h, sw_attrs(), {
        sw_head(h, sw_attrs(), {
            render_head(h, "Syphax-Web Live Queue");
        });
        sw_body(h, sw_attrs(sw_attr("class", "page")), {
            sw_main(h, sw_attrs(sw_attr("class", "shell wide")), {
                sw_nav(h, sw_attrs(sw_attr("class", "nav")), {
                    render_language_links(h, "/", lang);
                });
                sw_section(h, sw_attrs(sw_attr("class", "hero compact")), {
                    sw_span(h, sw_attrs(sw_attr("class", "kicker secure")), { sw_text(h, "Live HTTPS"); });
                    sw_h1(h, sw_attrs(), { sw_text(h, "Interactive work queue"); });
                    sw_p(h, sw_attrs(sw_attr("class", "lead")), {
                        sw_text(h, "The handler reads query strings and form bodies, updates process memory, and returns partial HTML.");
                    });
                    sw_span(h, sw_attrs(sw_attr("class", "pill success")), {
                        sw_text(h, seen[0] != '\0' ? "Welcome back" : "First visit");
                    });
                });

                sw_section(h, sw_attrs(sw_attr("class", "grid two")), {
                    sw_form(h, sw_attrs(
                        sw_attr("id", "item-form"),
                        sw_attr("class", "card form-stack"),
                        sw_attr("method", "post"),
                        sw_attr("action", "/items")
                    ), {
                        sw_input(h, sw_attrs(
                            sw_attr("name", "lang"),
                            sw_attr("type", "hidden"),
                            sw_attr("value", lang)
                        ));
                        sw_h2(h, sw_attrs(), { sw_text(h, "Add Item"); });
                        sw_label(h, sw_attrs(sw_attr("class", "field")), {
                            sw_span(h, sw_attrs(), { sw_text(h, "Name"); });
                            sw_input(h, sw_attrs(sw_attr("name", "name"), sw_attr("type", "text"), sw_attr("autocomplete", "off")));
                        });
                        sw_label(h, sw_attrs(sw_attr("class", "field")), {
                            sw_span(h, sw_attrs(), { sw_text(h, "Owner"); });
                            sw_input(h, sw_attrs(sw_attr("name", "owner"), sw_attr("type", "text"), sw_attr("autocomplete", "off")));
                        });
                        sw_label(h, sw_attrs(sw_attr("class", "field")), {
                            sw_span(h, sw_attrs(), { sw_text(h, "Priority"); });
                            sw_select(h, sw_attrs(sw_attr("name", "severity")), {
                                sw_option(h, sw_attrs(sw_attr("value", "High")), { sw_text(h, "High"); });
                                sw_option(h, sw_attrs(sw_attr("value", "Medium")), { sw_text(h, "Medium"); });
                                sw_option(h, sw_attrs(sw_attr("value", "Low")), { sw_text(h, "Low"); });
                            });
                        });
                        sw_button(h, sw_attrs(sw_attr("class", "button"), sw_attr("type", "submit")), {
                            sw_text(h, "Add");
                        });
                    });

                    sw_form(h, sw_attrs(
                        sw_attr("id", "filter-form"),
                        sw_attr("class", "card form-stack"),
                        sw_attr("method", "get"),
                        sw_attr("action", "/items")
                    ), {
                        sw_input(h, sw_attrs(
                            sw_attr("name", "lang"),
                            sw_attr("type", "hidden"),
                            sw_attr("value", lang)
                        ));
                        sw_h2(h, sw_attrs(), { sw_text(h, "Filter"); });
                        sw_label(h, sw_attrs(sw_attr("class", "field")), {
                            sw_span(h, sw_attrs(), { sw_text(h, "Query"); });
                            sw_input(h, sw_attrs(
                                sw_attr("id", "item-query"),
                                sw_attr("name", "q"),
                                sw_attr("type", "text"),
                                sw_attr("autocomplete", "off")
                            ));
                        });
                    });
                });

                sw_div(h, sw_attrs(sw_attr("id", "items-panel"), sw_attr("class", "preview-region")), {
                    render_items(h, state, "", NULL);
                });

                (void)sw_js_live_search(h, "filter-form", "item-query", "items-panel", "/items");
                (void)sw_js_fetch(h,
                    .trigger_id = "item-form",
                    .form_id = "item-form",
                    .target_id = "items-panel",
                    .endpoint = "/items",
                    .loading_class = "is-loading",
                    .event_type = SW_JS_SUBMIT,
                    .method = SW_JS_POST,
                    .swap_mode = SW_JS_INNER,
                    .serialize_form = 1,
                    .abort_stale = 1,
                    .prevent_default = 1
                );
            });
        });
    });

    (void)sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
    sw_buffer_free(h);
}

static void handle_request(sw_connection* connection, const sw_http_message* request, void* user_data) {
    app_state* state = (app_state*)user_data;
    c8 query[128];
    c8 message[64];

    if (serve_style(connection, request)) {
        return;
    }
    if (sw_http_is(request, "GET", "/")) {
        use_query_language(state != NULL ? state->translator : NULL, request);
        render_home(connection, state, request);
        return;
    }
    if (sw_http_is(request, "GET", "/items")) {
        use_query_language(state != NULL ? state->translator : NULL, request);
        (void)sw_http_get_query(request, "q", query, sizeof(query));
        reply_items(connection, state, query, NULL);
        return;
    }
    if (sw_http_is(request, "POST", "/items")) {
        use_form_language(state != NULL ? state->translator : NULL, request);
        add_item(state, request, message, sizeof(message));
        reply_items(connection, state, "", message);
        return;
    }

    reply_not_found(connection);
}

int main(void) {
    sw_http_config config = http_config();
    app_state state;
    i32 rc;

    memset(&state, 0, sizeof(state));
    state.translator = example_translator();
    if (state.translator == NULL) {
        fprintf(stderr, "Failed to load translations.\n");
        return 1;
    }
    seed_items(&state);
    rc = listen_https(
        EXAMPLE_HTTPS_URL,
        &config,
        handle_request,
        &state,
        "Syphax-Web live queue example"
    );
    sw_translator_destroy(state.translator);
    return rc;
}
