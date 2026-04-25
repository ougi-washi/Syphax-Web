#include "example_common.h"
#include "sw_db.h"
#include "sw_html.h"
#include "sw_server.h"

#include <stdio.h>
#include <stdlib.h>

static i64 visit_count(sw_db* db) {
    sw_db_stmt* stmt;
    i64 count = 0;

    if (sw_db_exec(db, "INSERT INTO sw_visits(note) VALUES('page')") != 0) {
        return -1;
    }

    stmt = sw_db_prepare(db, "SELECT COUNT(*) FROM sw_visits");
    if (stmt == NULL) {
        return -1;
    }
    if (sw_db_step(stmt) == SW_DB_ROW) {
        count = sw_db_column_int(stmt, 0);
    }
    sw_db_finalize(stmt);
    return count;
}

static void handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    sw_db* db = (sw_db*)user_data;
    sw_buffer* h;
    i64 count;

    if (!sw_http_is(request, "GET", "/")) {
        sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "Not Found");
        return;
    }

    count = visit_count(db);
    h = sw_buffer_new();
    sw_html(h, sw_attrs(sw_attr("lang", "en")), {
        sw_body(h, sw_attrs(), {
            sw_h1(h, sw_attrs(), {
                sw_text(h, "Database");
            });
            sw_p(h, sw_attrs(), {
                if (count >= 0) {
                    sw_rawf(h, "Driver: %s", sw_db_driver_name(sw_db_get_driver(db)));
                } else {
                    sw_text(h, "Database error");
                }
            });
            sw_p(h, sw_attrs(), {
                if (count >= 0) {
                    sw_rawf(h, "Visits: %lld", (long long)count);
                } else {
                    sw_text_no_translate(h, sw_db_error(db));
                }
            });
        });
    });

    sw_http_reply(connection, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
    sw_buffer_free(h);
}

int main(void) {
    const c8* url = getenv("SYPHAX_WEB_DB_URL");
    sw_db_config db_config = sw_db_config_default();
    sw_db* db;
    int rc;

    if (url != NULL && url[0] != '\0') {
        db_config.url = url;
    }

    db = sw_db_open(&db_config);
    if (db == NULL) {
        fprintf(stderr, "database open failed: %s\n", sw_db_error(NULL));
        fprintf(stderr, "Build with ./build.sh -sqlite or set SYPHAX_WEB_DB_URL for another enabled driver.\n");
        return 1;
    }

    if (sw_db_exec(db, "CREATE TABLE IF NOT EXISTS sw_visits (note TEXT NOT NULL)") != 0) {
        fprintf(stderr, "database setup failed: %s\n", sw_db_error(db));
        sw_db_close(db);
        return 1;
    }

    printf("Syphax-Web database example\n");
    printf("Open %s in your browser\n", EXAMPLE_HTTP_URL);
    rc = sw_server_listen(EXAMPLE_HTTP_URL, NULL, handler, db);
    sw_db_close(db);
    return rc;
}
