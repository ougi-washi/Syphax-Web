#include "sw_db_internal.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static c8 sw_db_global_error[512];

static b8 sw_db_url_starts(const c8* url, const c8* prefix) {
    const sz prefix_len = strlen(prefix);
    return url != NULL && strncmp(url, prefix, prefix_len) == 0;
}

static void sw_db_vset_error(c8* buffer, sz buffer_len, const c8* fmt, va_list args) {
    if (buffer == NULL || buffer_len == 0) {
        return;
    }
    if (fmt == NULL) {
        buffer[0] = '\0';
        return;
    }
    vsnprintf(buffer, buffer_len, fmt, args);
    buffer[buffer_len - 1] = '\0';
}

void sw_db_set_error(sw_db* db, const c8* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    if (db != NULL) {
        sw_db_vset_error(db->error, sizeof(db->error), fmt, args);
    } else {
        sw_db_vset_error(sw_db_global_error, sizeof(sw_db_global_error), fmt, args);
    }
    va_end(args);
}

void sw_db_set_global_error(const c8* fmt, ...) {
    va_list args;

    va_start(args, fmt);
    sw_db_vset_error(sw_db_global_error, sizeof(sw_db_global_error), fmt, args);
    va_end(args);
}

sw_db* sw_db_alloc(sw_db_driver driver, const sw_db_vtable* ops, const c8* url, void* handle) {
    sw_db* db = (sw_db*)calloc(1, sizeof(*db));

    if (db == NULL) {
        sw_db_set_global_error("out of memory");
        return NULL;
    }

    db->url = sw_strdup_cstr(url != NULL ? url : "");
    if (db->url == NULL) {
        free(db);
        sw_db_set_global_error("out of memory");
        return NULL;
    }

    db->driver = driver;
    db->ops = ops;
    db->handle = handle;
    db->error[0] = '\0';
    return db;
}

sw_db_config sw_db_config_default(void) {
    sw_db_config config;

    memset(&config, 0, sizeof(config));
    config.url = "sqlite://:memory:";
    config.busy_timeout_ms = 5000;
    return config;
}

sw_db* sw_db_open(const sw_db_config* config) {
    sw_db_config effective = sw_db_config_default();

    sw_db_set_global_error(NULL);

    if (config != NULL) {
        if (config->url != NULL) {
            effective.url = config->url;
        }
        if (config->busy_timeout_ms >= 0) {
            effective.busy_timeout_ms = config->busy_timeout_ms;
        }
    }

    if (effective.url == NULL || effective.url[0] == '\0') {
        sw_db_set_global_error("database url is empty");
        return NULL;
    }

    if (sw_db_url_starts(effective.url, "sqlite://")) {
#if defined(SYPHAX_WEB_HAS_SQLITE)
        sw_db* db = NULL;
        return sw_db_open_sqlite(&effective, &db) == 0 ? db : NULL;
#else
        sw_db_set_global_error("SQLite support is not enabled");
        return NULL;
#endif
    }

    if (sw_db_url_starts(effective.url, "postgres://") || sw_db_url_starts(effective.url, "postgresql://")) {
#if defined(SYPHAX_WEB_HAS_POSTGRES)
        sw_db* db = NULL;
        return sw_db_open_postgres(&effective, &db) == 0 ? db : NULL;
#else
        sw_db_set_global_error("PostgreSQL support is not enabled");
        return NULL;
#endif
    }

    sw_db_set_global_error("unsupported database url: %s", effective.url);
    return NULL;
}

void sw_db_close(sw_db* db) {
    if (db == NULL) {
        return;
    }
    if (db->ops != NULL && db->ops->close != NULL) {
        db->ops->close(db);
    }
    free(db->url);
    free(db);
}

sw_db_driver sw_db_get_driver(const sw_db* db) {
    return db != NULL ? db->driver : SW_DB_DRIVER_NONE;
}

const c8* sw_db_driver_name(sw_db_driver driver) {
    switch (driver) {
        case SW_DB_DRIVER_SQLITE:
            return "sqlite";
        case SW_DB_DRIVER_POSTGRES:
            return "postgres";
        case SW_DB_DRIVER_NONE:
        default:
            return "none";
    }
}

const c8* sw_db_error(const sw_db* db) {
    return db != NULL ? db->error : sw_db_global_error;
}

i32 sw_db_exec(sw_db* db, const c8* sql) {
    if (db == NULL || db->ops == NULL || db->ops->exec == NULL) {
        sw_db_set_error(db, "database is not open");
        return -1;
    }
    if (sql == NULL) {
        sw_db_set_error(db, "sql is null");
        return -1;
    }
    return db->ops->exec(db, sql);
}

sw_db_stmt* sw_db_prepare(sw_db* db, const c8* sql) {
    if (db == NULL || db->ops == NULL || db->ops->prepare == NULL) {
        sw_db_set_error(db, "database is not open");
        return NULL;
    }
    if (sql == NULL) {
        sw_db_set_error(db, "sql is null");
        return NULL;
    }
    return db->ops->prepare(db, sql);
}

void sw_db_finalize(sw_db_stmt* stmt) {
    if (stmt == NULL) {
        return;
    }
    if (stmt->db != NULL && stmt->db->ops != NULL && stmt->db->ops->finalize != NULL) {
        stmt->db->ops->finalize(stmt);
    }
    free(stmt);
}

i32 sw_db_reset(sw_db_stmt* stmt) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->reset == NULL) {
        return -1;
    }
    return stmt->db->ops->reset(stmt);
}

i32 sw_db_bind_null(sw_db_stmt* stmt, sz index) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->bind_null == NULL) {
        return -1;
    }
    return stmt->db->ops->bind_null(stmt, index);
}

i32 sw_db_bind_int(sw_db_stmt* stmt, sz index, i64 value) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->bind_int == NULL) {
        return -1;
    }
    return stmt->db->ops->bind_int(stmt, index, value);
}

i32 sw_db_bind_float(sw_db_stmt* stmt, sz index, f64 value) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->bind_float == NULL) {
        return -1;
    }
    return stmt->db->ops->bind_float(stmt, index, value);
}

i32 sw_db_bind_text(sw_db_stmt* stmt, sz index, const c8* value) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->bind_text == NULL) {
        return -1;
    }
    return stmt->db->ops->bind_text(stmt, index, value);
}

i32 sw_db_bind_blob(sw_db_stmt* stmt, sz index, const void* value, sz value_len) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->bind_blob == NULL) {
        return -1;
    }
    return stmt->db->ops->bind_blob(stmt, index, value, value_len);
}

sw_db_result sw_db_step(sw_db_stmt* stmt) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->step == NULL) {
        return SW_DB_ERROR;
    }
    return stmt->db->ops->step(stmt);
}

sz sw_db_column_count(const sw_db_stmt* stmt) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->column_count == NULL) {
        return 0;
    }
    return stmt->db->ops->column_count(stmt);
}

const c8* sw_db_column_name(const sw_db_stmt* stmt, sz index) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->column_name == NULL) {
        return NULL;
    }
    return stmt->db->ops->column_name(stmt, index);
}

sw_db_value_type sw_db_column_type(const sw_db_stmt* stmt, sz index) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->column_type == NULL) {
        return SW_DB_VALUE_NULL;
    }
    return stmt->db->ops->column_type(stmt, index);
}

i64 sw_db_column_int(const sw_db_stmt* stmt, sz index) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->column_int == NULL) {
        return 0;
    }
    return stmt->db->ops->column_int(stmt, index);
}

f64 sw_db_column_float(const sw_db_stmt* stmt, sz index) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->column_float == NULL) {
        return 0.0;
    }
    return stmt->db->ops->column_float(stmt, index);
}

const c8* sw_db_column_text(const sw_db_stmt* stmt, sz index) {
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->column_text == NULL) {
        return NULL;
    }
    return stmt->db->ops->column_text(stmt, index);
}

sz sw_db_column_blob(const sw_db_stmt* stmt, sz index, const void** out_data) {
    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (stmt == NULL || stmt->db == NULL || stmt->db->ops == NULL || stmt->db->ops->column_blob == NULL) {
        return 0;
    }
    return stmt->db->ops->column_blob(stmt, index, out_data);
}

i32 sw_db_begin(sw_db* db) {
    return sw_db_exec(db, "BEGIN");
}

i32 sw_db_commit(sw_db* db) {
    return sw_db_exec(db, "COMMIT");
}

i32 sw_db_rollback(sw_db* db) {
    return sw_db_exec(db, "ROLLBACK");
}

i64 sw_db_changes(const sw_db* db) {
    if (db == NULL || db->ops == NULL || db->ops->changes == NULL) {
        return 0;
    }
    return db->ops->changes(db);
}

i64 sw_db_last_insert_id(const sw_db* db) {
    if (db == NULL || db->ops == NULL || db->ops->last_insert_id == NULL) {
        return 0;
    }
    return db->ops->last_insert_id(db);
}
