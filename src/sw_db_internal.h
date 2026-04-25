#ifndef SW_DB_INTERNAL_H
#define SW_DB_INTERNAL_H

#include "sw_db.h"
#include "sw_internal.h"

typedef struct sw_db_vtable sw_db_vtable;

struct sw_db {
    sw_db_driver driver;
    const sw_db_vtable* ops;
    void* handle;
    c8* url;
    c8 error[512];
};

struct sw_db_stmt {
    sw_db* db;
    void* handle;
};

struct sw_db_vtable {
    void (*close)(sw_db* db);
    i32 (*exec)(sw_db* db, const c8* sql);
    sw_db_stmt* (*prepare)(sw_db* db, const c8* sql);
    void (*finalize)(sw_db_stmt* stmt);
    i32 (*reset)(sw_db_stmt* stmt);
    i32 (*bind_null)(sw_db_stmt* stmt, sz index);
    i32 (*bind_int)(sw_db_stmt* stmt, sz index, i64 value);
    i32 (*bind_float)(sw_db_stmt* stmt, sz index, f64 value);
    i32 (*bind_text)(sw_db_stmt* stmt, sz index, const c8* value);
    i32 (*bind_blob)(sw_db_stmt* stmt, sz index, const void* value, sz value_len);
    sw_db_result (*step)(sw_db_stmt* stmt);
    sz (*column_count)(const sw_db_stmt* stmt);
    const c8* (*column_name)(const sw_db_stmt* stmt, sz index);
    sw_db_value_type (*column_type)(const sw_db_stmt* stmt, sz index);
    i64 (*column_int)(const sw_db_stmt* stmt, sz index);
    f64 (*column_float)(const sw_db_stmt* stmt, sz index);
    const c8* (*column_text)(const sw_db_stmt* stmt, sz index);
    sz (*column_blob)(const sw_db_stmt* stmt, sz index, const void** out_data);
    i64 (*changes)(const sw_db* db);
    i64 (*last_insert_id)(const sw_db* db);
};

sw_db* sw_db_alloc(sw_db_driver driver, const sw_db_vtable* ops, const c8* url, void* handle);
void sw_db_set_error(sw_db* db, const c8* fmt, ...);
void sw_db_set_global_error(const c8* fmt, ...);

i32 sw_db_open_sqlite(const sw_db_config* config, sw_db** out_db);
i32 sw_db_open_postgres(const sw_db_config* config, sw_db** out_db);

#endif
