#include "sw_db_internal.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(SYPHAX_WEB_HAS_SQLITE)
#    include <sqlite3.h>

static void sw_sqlite_close(sw_db* db);
static i32 sw_sqlite_exec(sw_db* db, const c8* sql);
static sw_db_stmt* sw_sqlite_prepare(sw_db* db, const c8* sql);
static void sw_sqlite_finalize(sw_db_stmt* stmt);
static i32 sw_sqlite_reset(sw_db_stmt* stmt);
static i32 sw_sqlite_bind_null(sw_db_stmt* stmt, sz index);
static i32 sw_sqlite_bind_int(sw_db_stmt* stmt, sz index, i64 value);
static i32 sw_sqlite_bind_float(sw_db_stmt* stmt, sz index, f64 value);
static i32 sw_sqlite_bind_text(sw_db_stmt* stmt, sz index, const c8* value);
static i32 sw_sqlite_bind_blob(sw_db_stmt* stmt, sz index, const void* value, sz value_len);
static sw_db_result sw_sqlite_step(sw_db_stmt* stmt);
static sz sw_sqlite_column_count(const sw_db_stmt* stmt);
static const c8* sw_sqlite_column_name(const sw_db_stmt* stmt, sz index);
static sw_db_value_type sw_sqlite_column_type(const sw_db_stmt* stmt, sz index);
static i64 sw_sqlite_column_int(const sw_db_stmt* stmt, sz index);
static f64 sw_sqlite_column_float(const sw_db_stmt* stmt, sz index);
static const c8* sw_sqlite_column_text(const sw_db_stmt* stmt, sz index);
static sz sw_sqlite_column_blob(const sw_db_stmt* stmt, sz index, const void** out_data);
static i64 sw_sqlite_changes(const sw_db* db);
static i64 sw_sqlite_last_insert_id(const sw_db* db);

static const sw_db_vtable sw_sqlite_ops = {
    sw_sqlite_close,
    sw_sqlite_exec,
    sw_sqlite_prepare,
    sw_sqlite_finalize,
    sw_sqlite_reset,
    sw_sqlite_bind_null,
    sw_sqlite_bind_int,
    sw_sqlite_bind_float,
    sw_sqlite_bind_text,
    sw_sqlite_bind_blob,
    sw_sqlite_step,
    sw_sqlite_column_count,
    sw_sqlite_column_name,
    sw_sqlite_column_type,
    sw_sqlite_column_int,
    sw_sqlite_column_float,
    sw_sqlite_column_text,
    sw_sqlite_column_blob,
    sw_sqlite_changes,
    sw_sqlite_last_insert_id
};

static const c8* sw_sqlite_path_from_url(const c8* url) {
    const c8* path;

    if (url == NULL || strncmp(url, "sqlite://", 9) != 0) {
        return NULL;
    }

    path = url + 9;
    return path[0] != '\0' ? path : NULL;
}

static int sw_sqlite_bind_index(sw_db_stmt* stmt, sz index) {
    sqlite3_stmt* sqlite_stmt;
    int parameter_count;

    if (stmt == NULL || stmt->handle == NULL || index == 0 || index > (sz)INT_MAX) {
        return -1;
    }

    sqlite_stmt = (sqlite3_stmt*)stmt->handle;
    parameter_count = sqlite3_bind_parameter_count(sqlite_stmt);
    if ((int)index > parameter_count) {
        sw_db_set_error(stmt->db, "bind index out of range");
        return -1;
    }

    return (int)index;
}

static int sw_sqlite_column_index(const sw_db_stmt* stmt, sz index) {
    sqlite3_stmt* sqlite_stmt;
    int column_count;

    if (stmt == NULL || stmt->handle == NULL || index > (sz)INT_MAX) {
        return -1;
    }

    sqlite_stmt = (sqlite3_stmt*)stmt->handle;
    column_count = sqlite3_column_count(sqlite_stmt);
    if ((int)index >= column_count) {
        return -1;
    }

    return (int)index;
}

static i32 sw_sqlite_bind_result(sw_db_stmt* stmt, int rc, const c8* action) {
    if (rc == SQLITE_OK) {
        return 0;
    }

    sw_db_set_error(
        stmt != NULL ? stmt->db : NULL,
        "SQLite %s failed: %s",
        action != NULL ? action : "bind",
        stmt != NULL && stmt->db != NULL && stmt->db->handle != NULL
            ? sqlite3_errmsg((sqlite3*)stmt->db->handle)
            : "unknown error"
    );
    return -1;
}

i32 sw_db_open_sqlite(const sw_db_config* config, sw_db** out_db) {
    sqlite3* sqlite = NULL;
    sw_db* db;
    const c8* path;
    int rc;

    if (out_db != NULL) {
        *out_db = NULL;
    }
    if (config == NULL || out_db == NULL) {
        sw_db_set_global_error("invalid SQLite configuration");
        return -1;
    }

    path = sw_sqlite_path_from_url(config->url);
    if (path == NULL) {
        sw_db_set_global_error("invalid SQLite url");
        return -1;
    }

    rc = sqlite3_open_v2(path, &sqlite, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, NULL);
    if (rc != SQLITE_OK) {
        sw_db_set_global_error("SQLite open failed: %s", sqlite != NULL ? sqlite3_errmsg(sqlite) : "unknown error");
        if (sqlite != NULL) {
            sqlite3_close(sqlite);
        }
        return -1;
    }

    if (config->busy_timeout_ms > 0) {
        sqlite3_busy_timeout(sqlite, config->busy_timeout_ms);
    }

    db = sw_db_alloc(SW_DB_DRIVER_SQLITE, &sw_sqlite_ops, config->url, sqlite);
    if (db == NULL) {
        sqlite3_close(sqlite);
        return -1;
    }

    *out_db = db;
    return 0;
}

static void sw_sqlite_close(sw_db* db) {
    if (db != NULL && db->handle != NULL) {
        sqlite3_close((sqlite3*)db->handle);
        db->handle = NULL;
    }
}

static i32 sw_sqlite_exec(sw_db* db, const c8* sql) {
    char* error = NULL;
    int rc;

    rc = sqlite3_exec((sqlite3*)db->handle, sql, NULL, NULL, &error);
    if (rc != SQLITE_OK) {
        sw_db_set_error(db, "SQLite exec failed: %s", error != NULL ? error : sqlite3_errmsg((sqlite3*)db->handle));
        sqlite3_free(error);
        return -1;
    }

    sqlite3_free(error);
    sw_db_set_error(db, NULL);
    return 0;
}

static sw_db_stmt* sw_sqlite_prepare(sw_db* db, const c8* sql) {
    sqlite3_stmt* sqlite_stmt = NULL;
    sw_db_stmt* stmt;
    int rc;

    rc = sqlite3_prepare_v2((sqlite3*)db->handle, sql, -1, &sqlite_stmt, NULL);
    if (rc != SQLITE_OK) {
        sw_db_set_error(db, "SQLite prepare failed: %s", sqlite3_errmsg((sqlite3*)db->handle));
        return NULL;
    }

    stmt = (sw_db_stmt*)calloc(1, sizeof(*stmt));
    if (stmt == NULL) {
        sqlite3_finalize(sqlite_stmt);
        sw_db_set_error(db, "out of memory");
        return NULL;
    }

    stmt->db = db;
    stmt->handle = sqlite_stmt;
    sw_db_set_error(db, NULL);
    return stmt;
}

static void sw_sqlite_finalize(sw_db_stmt* stmt) {
    if (stmt != NULL && stmt->handle != NULL) {
        sqlite3_finalize((sqlite3_stmt*)stmt->handle);
        stmt->handle = NULL;
    }
}

static i32 sw_sqlite_reset(sw_db_stmt* stmt) {
    int rc;

    if (stmt == NULL || stmt->handle == NULL) {
        return -1;
    }

    rc = sqlite3_reset((sqlite3_stmt*)stmt->handle);
    if (rc != SQLITE_OK) {
        sw_db_set_error(stmt->db, "SQLite reset failed: %s", sqlite3_errmsg((sqlite3*)stmt->db->handle));
        return -1;
    }

    return 0;
}

static i32 sw_sqlite_bind_null(sw_db_stmt* stmt, sz index) {
    const int bind_index = sw_sqlite_bind_index(stmt, index);
    int rc;

    if (bind_index < 0) {
        return -1;
    }
    rc = sqlite3_bind_null((sqlite3_stmt*)stmt->handle, bind_index);
    return sw_sqlite_bind_result(stmt, rc, "bind null");
}

static i32 sw_sqlite_bind_int(sw_db_stmt* stmt, sz index, i64 value) {
    const int bind_index = sw_sqlite_bind_index(stmt, index);
    int rc;

    if (bind_index < 0) {
        return -1;
    }
    rc = sqlite3_bind_int64((sqlite3_stmt*)stmt->handle, bind_index, (sqlite3_int64)value);
    return sw_sqlite_bind_result(stmt, rc, "bind int");
}

static i32 sw_sqlite_bind_float(sw_db_stmt* stmt, sz index, f64 value) {
    const int bind_index = sw_sqlite_bind_index(stmt, index);
    int rc;

    if (bind_index < 0) {
        return -1;
    }
    rc = sqlite3_bind_double((sqlite3_stmt*)stmt->handle, bind_index, value);
    return sw_sqlite_bind_result(stmt, rc, "bind float");
}

static i32 sw_sqlite_bind_text(sw_db_stmt* stmt, sz index, const c8* value) {
    const int bind_index = sw_sqlite_bind_index(stmt, index);
    int rc;

    if (bind_index < 0) {
        return -1;
    }
    if (value == NULL) {
        return sw_sqlite_bind_null(stmt, index);
    }
    rc = sqlite3_bind_text((sqlite3_stmt*)stmt->handle, bind_index, value, -1, SQLITE_TRANSIENT);
    return sw_sqlite_bind_result(stmt, rc, "bind text");
}

static i32 sw_sqlite_bind_blob(sw_db_stmt* stmt, sz index, const void* value, sz value_len) {
    const int bind_index = sw_sqlite_bind_index(stmt, index);
    int rc;

    if (bind_index < 0) {
        return -1;
    }
    if (value == NULL) {
        return sw_sqlite_bind_null(stmt, index);
    }
    if (value_len > (sz)INT_MAX) {
        sw_db_set_error(stmt->db, "blob value is too large");
        return -1;
    }
    rc = sqlite3_bind_blob((sqlite3_stmt*)stmt->handle, bind_index, value, (int)value_len, SQLITE_TRANSIENT);
    return sw_sqlite_bind_result(stmt, rc, "bind blob");
}

static sw_db_result sw_sqlite_step(sw_db_stmt* stmt) {
    const int rc = sqlite3_step((sqlite3_stmt*)stmt->handle);

    if (rc == SQLITE_ROW) {
        return SW_DB_ROW;
    }
    if (rc == SQLITE_DONE) {
        return SW_DB_DONE;
    }

    sw_db_set_error(stmt->db, "SQLite step failed: %s", sqlite3_errmsg((sqlite3*)stmt->db->handle));
    return SW_DB_ERROR;
}

static sz sw_sqlite_column_count(const sw_db_stmt* stmt) {
    return stmt != NULL && stmt->handle != NULL ? (sz)sqlite3_column_count((sqlite3_stmt*)stmt->handle) : 0;
}

static const c8* sw_sqlite_column_name(const sw_db_stmt* stmt, sz index) {
    const int column_index = sw_sqlite_column_index(stmt, index);
    return column_index >= 0 ? sqlite3_column_name((sqlite3_stmt*)stmt->handle, column_index) : NULL;
}

static sw_db_value_type sw_sqlite_column_type(const sw_db_stmt* stmt, sz index) {
    const int column_index = sw_sqlite_column_index(stmt, index);

    if (column_index < 0) {
        return SW_DB_VALUE_NULL;
    }

    switch (sqlite3_column_type((sqlite3_stmt*)stmt->handle, column_index)) {
        case SQLITE_INTEGER:
            return SW_DB_VALUE_INTEGER;
        case SQLITE_FLOAT:
            return SW_DB_VALUE_FLOAT;
        case SQLITE_TEXT:
            return SW_DB_VALUE_TEXT;
        case SQLITE_BLOB:
            return SW_DB_VALUE_BLOB;
        case SQLITE_NULL:
        default:
            return SW_DB_VALUE_NULL;
    }
}

static i64 sw_sqlite_column_int(const sw_db_stmt* stmt, sz index) {
    const int column_index = sw_sqlite_column_index(stmt, index);
    return column_index >= 0 ? (i64)sqlite3_column_int64((sqlite3_stmt*)stmt->handle, column_index) : 0;
}

static f64 sw_sqlite_column_float(const sw_db_stmt* stmt, sz index) {
    const int column_index = sw_sqlite_column_index(stmt, index);
    return column_index >= 0 ? sqlite3_column_double((sqlite3_stmt*)stmt->handle, column_index) : 0.0;
}

static const c8* sw_sqlite_column_text(const sw_db_stmt* stmt, sz index) {
    const int column_index = sw_sqlite_column_index(stmt, index);
    return column_index >= 0 ? (const c8*)sqlite3_column_text((sqlite3_stmt*)stmt->handle, column_index) : NULL;
}

static sz sw_sqlite_column_blob(const sw_db_stmt* stmt, sz index, const void** out_data) {
    const int column_index = sw_sqlite_column_index(stmt, index);
    const void* data;
    int len;

    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (column_index < 0) {
        return 0;
    }

    data = sqlite3_column_blob((sqlite3_stmt*)stmt->handle, column_index);
    len = sqlite3_column_bytes((sqlite3_stmt*)stmt->handle, column_index);
    if (out_data != NULL) {
        *out_data = data;
    }
    return len > 0 ? (sz)len : 0;
}

static i64 sw_sqlite_changes(const sw_db* db) {
    return db != NULL && db->handle != NULL ? (i64)sqlite3_changes((sqlite3*)db->handle) : 0;
}

static i64 sw_sqlite_last_insert_id(const sw_db* db) {
    return db != NULL && db->handle != NULL ? (i64)sqlite3_last_insert_rowid((sqlite3*)db->handle) : 0;
}

#else

i32 sw_db_open_sqlite(const sw_db_config* config, sw_db** out_db) {
    (void)config;
    if (out_db != NULL) {
        *out_db = NULL;
    }
    sw_db_set_global_error("SQLite support is not enabled");
    return -1;
}

#endif
