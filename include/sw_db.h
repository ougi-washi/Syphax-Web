#ifndef SW_DB_H
#define SW_DB_H

#include "sw_export.h"
#include "syphax/s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sw_db sw_db;
typedef struct sw_db_stmt sw_db_stmt;

typedef enum sw_db_driver {
    SW_DB_DRIVER_NONE,
    SW_DB_DRIVER_SQLITE,
    SW_DB_DRIVER_POSTGRES
} sw_db_driver;

typedef enum sw_db_result {
    SW_DB_ERROR = -1,
    SW_DB_DONE = 0,
    SW_DB_ROW = 1
} sw_db_result;

typedef enum sw_db_value_type {
    SW_DB_VALUE_NULL,
    SW_DB_VALUE_INTEGER,
    SW_DB_VALUE_FLOAT,
    SW_DB_VALUE_TEXT,
    SW_DB_VALUE_BLOB
} sw_db_value_type;

typedef struct sw_db_config {
    /*
     * URL forms: sqlite://:memory:, sqlite://path.db, postgres://...,
     * postgresql://...
     */
    const c8* url;

    /* SQLite-only busy timeout. Default is 5000 ms; 0 disables it. */
    i32 busy_timeout_ms;
} sw_db_config;

/*
 * NULL config uses sw_db_config_default(), which opens sqlite://:memory: when
 * SQLite is enabled.
 */
SW_API sw_db_config sw_db_config_default(void);
SW_API sw_db* sw_db_open(const sw_db_config* config);
SW_API void sw_db_close(sw_db* db);
SW_API sw_db_driver sw_db_get_driver(const sw_db* db);
SW_API const c8* sw_db_driver_name(sw_db_driver driver);
SW_API const c8* sw_db_error(const sw_db* db);

SW_API i32 sw_db_exec(sw_db* db, const c8* sql);
SW_API sw_db_stmt* sw_db_prepare(sw_db* db, const c8* sql);
SW_API void sw_db_finalize(sw_db_stmt* stmt);
SW_API i32 sw_db_reset(sw_db_stmt* stmt);

/*
 * Prepared SQL uses ? placeholders for every driver. Bind indexes are 1-based,
 * matching SQL placeholder order.
 */
SW_API i32 sw_db_bind_null(sw_db_stmt* stmt, sz index);
SW_API i32 sw_db_bind_int(sw_db_stmt* stmt, sz index, i64 value);
SW_API i32 sw_db_bind_float(sw_db_stmt* stmt, sz index, f64 value);
SW_API i32 sw_db_bind_text(sw_db_stmt* stmt, sz index, const c8* value);
SW_API i32 sw_db_bind_blob(sw_db_stmt* stmt, sz index, const void* value, sz value_len);

/*
 * Column indexes are 0-based, matching result column order.
 * Returned text/blob pointers are owned by the statement and stay valid until
 * the next step/reset/finalize call on that statement.
 */
SW_API sw_db_result sw_db_step(sw_db_stmt* stmt);
SW_API sz sw_db_column_count(const sw_db_stmt* stmt);
SW_API const c8* sw_db_column_name(const sw_db_stmt* stmt, sz index);
SW_API sw_db_value_type sw_db_column_type(const sw_db_stmt* stmt, sz index);
SW_API i64 sw_db_column_int(const sw_db_stmt* stmt, sz index);
SW_API f64 sw_db_column_float(const sw_db_stmt* stmt, sz index);
SW_API const c8* sw_db_column_text(const sw_db_stmt* stmt, sz index);
SW_API sz sw_db_column_blob(const sw_db_stmt* stmt, sz index, const void** out_data);

SW_API i32 sw_db_begin(sw_db* db);
SW_API i32 sw_db_commit(sw_db* db);
SW_API i32 sw_db_rollback(sw_db* db);
SW_API i64 sw_db_changes(const sw_db* db);
/*
 * SQLite returns sqlite3_last_insert_rowid(); PostgreSQL users should use
 * RETURNING.
 */
SW_API i64 sw_db_last_insert_id(const sw_db* db);

#ifdef __cplusplus
}
#endif

#endif
