#include "example_common.h"

typedef struct {
    c8 label[80];
    c8 value[80];
} user_item;

typedef struct {
    const c8* id;
    const c8* name;
    const c8* password;
    user_item items[12];
    sz item_count;
} demo_user;

typedef struct {
    sw_sessions* sessions;
    demo_user users[2];
} app_state;

static void seed_users(app_state* state) {
    if (state == NULL) {
        return;
    }

    state->users[0].id = "alice";
    state->users[0].name = "Alice";
    state->users[0].password = "alice";
    state->users[0].item_count = 2;
    copy_text(state->users[0].items[0].label, sizeof(state->users[0].items[0].label), "Plan");
    copy_text(state->users[0].items[0].value, sizeof(state->users[0].items[0].value), "Release dashboard");
    copy_text(state->users[0].items[1].label, sizeof(state->users[0].items[1].label), "Region");
    copy_text(state->users[0].items[1].value, sizeof(state->users[0].items[1].value), "Tokyo");

    state->users[1].id = "bob";
    state->users[1].name = "Bob";
    state->users[1].password = "bob";
    state->users[1].item_count = 2;
    copy_text(state->users[1].items[0].label, sizeof(state->users[1].items[0].label), "Plan");
    copy_text(state->users[1].items[0].value, sizeof(state->users[1].items[0].value), "Audit forms");
    copy_text(state->users[1].items[1].label, sizeof(state->users[1].items[1].label), "Region");
    copy_text(state->users[1].items[1].value, sizeof(state->users[1].items[1].value), "Casablanca");
}

static demo_user* find_user(app_state* state, const c8* id) {
    sz i;

    if (state == NULL || id == NULL || id[0] == '\0') {
        return NULL;
    }
    for (i = 0; i < COUNT_OF(state->users); ++i) {
        if (state->users[i].id != NULL && strcmp(state->users[i].id, id) == 0) {
            return &state->users[i];
        }
    }
    return NULL;
}

static demo_user* current_user(app_state* state, const sw_http_message* request) {
    sw_session* session;
    const c8* user_id;

    if (state == NULL || state->sessions == NULL) {
        return NULL;
    }
    session = sw_sessions_find(state->sessions, request);
    user_id = sw_session_get(session, "user_id");
    return find_user(state, user_id);
}

static void render_login(sw_connection* connection, const c8* message) {
    sw_buffer* h = sw_buffer_new();

    sw_html(h, sw_attrs(sw_attr("lang", "en")), {
        sw_head(h, sw_attrs(), {
            render_head(h, "Syphax-Web Sessions");
        });
        sw_body(h, sw_attrs(sw_attr("class", "page")), {
            sw_main(h, sw_attrs(sw_attr("class", "shell")), {
                sw_section(h, sw_attrs(sw_attr("class", "hero")), {
                    sw_span(h, sw_attrs(sw_attr("class", "kicker")), { sw_text(h, "Sessions"); });
                    sw_h1(h, sw_attrs(), { sw_text(h, "Login keeps server data separate"); });
                    sw_p(h, sw_attrs(sw_attr("class", "lead")), {
                        sw_text(h, "Use alice/alice or bob/bob. Each account has a separate in-memory list on the server.");
                    });
                });

                if (message != NULL && message[0] != '\0') {
                    sw_div(h, sw_attrs(sw_attr("class", "notice")), {
                        sw_text(h, message);
                    });
                }

                sw_form(h, sw_attrs(
                    sw_attr("class", "card form-stack"),
                    sw_attr("method", "post"),
                    sw_attr("action", "/login")
                ), {
                    sw_h2(h, sw_attrs(), { sw_text(h, "Login"); });
                    sw_label(h, sw_attrs(sw_attr("class", "field")), {
                        sw_span(h, sw_attrs(), { sw_text(h, "User"); });
                        sw_input(h, sw_attrs(sw_attr("name", "user"), sw_attr("type", "text"), sw_attr("autocomplete", "username")));
                    });
                    sw_label(h, sw_attrs(sw_attr("class", "field")), {
                        sw_span(h, sw_attrs(), { sw_text(h, "Password"); });
                        sw_input(h, sw_attrs(sw_attr("name", "password"), sw_attr("type", "password"), sw_attr("autocomplete", "current-password")));
                    });
                    sw_button(h, sw_attrs(sw_attr("class", "button"), sw_attr("type", "submit")), {
                        sw_text(h, "Login");
                    });
                });
            });
        });
    });

    (void)sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
    sw_buffer_free(h);
}

static void render_user_data(sw_buffer* h, const demo_user* user) {
    sz i;

    sw_div(h, sw_attrs(sw_attr("class", "panel")), {
        sw_div(h, sw_attrs(sw_attr("class", "panel-title")), {
            sw_h2(h, sw_attrs(), { sw_text(h, "Server Data"); });
            sw_span(h, sw_attrs(sw_attr("class", "pill success")), {
                sw_text_no_translate(h, user != NULL ? user->id : "");
            });
        });

        sw_div(h, sw_attrs(sw_attr("class", "table")), {
            sw_div(h, sw_attrs(sw_attr("class", "table-row table-head")), {
                sw_span(h, sw_attrs(), { sw_text(h, "Name"); });
                sw_span(h, sw_attrs(), { sw_text(h, "Value"); });
                sw_span(h, sw_attrs(), { sw_text(h, "Owner"); });
            });
            for (i = 0; user != NULL && i < user->item_count; ++i) {
                sw_div(h, sw_attrs(sw_attr("class", "table-row")), {
                    sw_span(h, sw_attrs(), { sw_text_no_translate(h, user->items[i].label); });
                    sw_span(h, sw_attrs(), { sw_text_no_translate(h, user->items[i].value); });
                    sw_span(h, sw_attrs(sw_attr("class", "pill")), { sw_text_no_translate(h, user->name); });
                });
            }
        });

        if (user == NULL || user->item_count == 0) {
            sw_div(h, sw_attrs(sw_attr("class", "empty-state")), {
                sw_text(h, "No saved data for this user.");
            });
        }
    });
}

static void render_dashboard(sw_connection* connection, const demo_user* user, const c8* message) {
    sw_buffer* h = sw_buffer_new();

    sw_html(h, sw_attrs(sw_attr("lang", "en")), {
        sw_head(h, sw_attrs(), {
            render_head(h, "Syphax-Web Sessions");
        });
        sw_body(h, sw_attrs(sw_attr("class", "page")), {
            sw_main(h, sw_attrs(sw_attr("class", "shell wide")), {
                sw_section(h, sw_attrs(sw_attr("class", "hero compact")), {
                    sw_span(h, sw_attrs(sw_attr("class", "kicker secure")), { sw_text(h, "Logged In"); });
                    sw_h1(h, sw_attrs(), { sw_text_no_translate(h, user != NULL ? user->name : "User"); });
                    sw_p(h, sw_attrs(sw_attr("class", "lead")), {
                        sw_text(h, "The session cookie only identifies the visit. The rows below live in process memory on the server.");
                    });
                    sw_form(h, sw_attrs(sw_attr("method", "post"), sw_attr("action", "/logout")), {
                        sw_button(h, sw_attrs(sw_attr("class", "button"), sw_attr("type", "submit")), {
                            sw_text(h, "Logout");
                        });
                    });
                });

                if (message != NULL && message[0] != '\0') {
                    sw_div(h, sw_attrs(sw_attr("class", "notice")), {
                        sw_text(h, message);
                    });
                }

                sw_section(h, sw_attrs(sw_attr("class", "grid two")), {
                    sw_form(h, sw_attrs(
                        sw_attr("class", "card form-stack"),
                        sw_attr("method", "post"),
                        sw_attr("action", "/data")
                    ), {
                        sw_h2(h, sw_attrs(), { sw_text(h, "Add Data"); });
                        sw_label(h, sw_attrs(sw_attr("class", "field")), {
                            sw_span(h, sw_attrs(), { sw_text(h, "Name"); });
                            sw_input(h, sw_attrs(sw_attr("name", "label"), sw_attr("type", "text"), sw_attr("autocomplete", "off")));
                        });
                        sw_label(h, sw_attrs(sw_attr("class", "field")), {
                            sw_span(h, sw_attrs(), { sw_text(h, "Value"); });
                            sw_input(h, sw_attrs(sw_attr("name", "value"), sw_attr("type", "text"), sw_attr("autocomplete", "off")));
                        });
                        sw_button(h, sw_attrs(sw_attr("class", "button"), sw_attr("type", "submit")), {
                            sw_text(h, "Save");
                        });
                    });

                    sw_article(h, sw_attrs(sw_attr("class", "card")), {
                        sw_h2(h, sw_attrs(), { sw_text(h, "Session"); });
                        sw_p(h, sw_attrs(), {
                            sw_text(h, "Log in as Bob in another browser or private window to see a different server-side list.");
                        });
                    });
                });

                render_user_data(h, user);
            });
        });
    });

    (void)sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
    sw_buffer_free(h);
}

static void login_user(app_state* state, sw_connection* connection, const sw_http_message* request) {
    c8 user_id[48];
    c8 password[48];
    demo_user* user;
    sw_session* session;

    (void)sw_http_get_form(request, "user", user_id, sizeof(user_id));
    (void)sw_http_get_form(request, "password", password, sizeof(password));

    user = find_user(state, user_id);
    if (user == NULL || strcmp(user->password, password) != 0) {
        render_login(connection, "Invalid login");
        return;
    }

    session = sw_sessions_start(state->sessions, connection, request);
    if (session == NULL || sw_session_set(session, "user_id", user->id) != 0) {
        (void)sw_http_replyf(connection, 500, "text/plain; charset=utf-8", "Session failed");
        return;
    }

    render_dashboard(connection, user, "Logged in");
}

static void add_user_data(app_state* state, sw_connection* connection, const sw_http_message* request) {
    demo_user* user = current_user(state, request);
    c8 label[80];
    c8 value[80];
    user_item* item;

    if (user == NULL) {
        render_login(connection, "Login required");
        return;
    }

    (void)sw_http_get_form(request, "label", label, sizeof(label));
    (void)sw_http_get_form(request, "value", value, sizeof(value));
    if (label[0] == '\0' || value[0] == '\0') {
        render_dashboard(connection, user, "Name and value required");
        return;
    }
    if (user->item_count >= COUNT_OF(user->items)) {
        render_dashboard(connection, user, "List full");
        return;
    }

    item = &user->items[user->item_count++];
    copy_text(item->label, sizeof(item->label), label);
    copy_text(item->value, sizeof(item->value), value);
    render_dashboard(connection, user, "Saved");
}

static void handle_request(sw_connection* connection, const sw_http_message* request, void* user_data) {
    app_state* state = (app_state*)user_data;
    demo_user* user;

    if (serve_style(connection, request)) {
        return;
    }
    if (sw_http_is(request, "GET", "/")) {
        user = current_user(state, request);
        if (user == NULL) {
            render_login(connection, NULL);
        } else {
            render_dashboard(connection, user, NULL);
        }
        return;
    }
    if (sw_http_is(request, "POST", "/login")) {
        login_user(state, connection, request);
        return;
    }
    if (sw_http_is(request, "POST", "/logout")) {
        if (state != NULL && state->sessions != NULL) {
            (void)sw_sessions_end(state->sessions, connection, request);
        }
        render_login(connection, "Logged out");
        return;
    }
    if (sw_http_is(request, "POST", "/data")) {
        add_user_data(state, connection, request);
        return;
    }

    reply_not_found(connection);
}

int main(void) {
    sw_server_config http = server_config();
    sw_session_config sessions = sw_session_config_default();
    app_state state;
    i32 rc;

    memset(&state, 0, sizeof(state));
    sessions.cookie_name = "sw_login";
    sessions.ttl_seconds = 3600;
    state.sessions = sw_sessions_create(&sessions);
    if (state.sessions == NULL) {
        fprintf(stderr, "Failed to create sessions.\n");
        return 1;
    }
    seed_users(&state);

    rc = listen_https(
        EXAMPLE_HTTPS_URL,
        &http,
        handle_request,
        &state,
        "Syphax-Web session login example"
    );
    sw_sessions_destroy(state.sessions);
    return rc;
}
