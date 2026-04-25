#include "sw_db_internal.h"

#include <ctype.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(SYPHAX_WEB_HAS_POSTGRES)
#    include <libpq-fe.h>

enum {
    SW_PG_BYTEA_OID = 17,
    SW_PG_INT2_OID = 21,
    SW_PG_INT4_OID = 23,
    SW_PG_INT8_OID = 20,
    SW_PG_FLOAT4_OID = 700,
    SW_PG_FLOAT8_OID = 701,
    SW_PG_NUMERIC_OID = 1700
};

typedef struct {
    PGconn* connection;
    i64 changes;
} sw_pg_db;

typedef struct {
    c8* data;
    int len;
    int format;
} sw_pg_param;

typedef struct {
    c8* sql;
    sz param_count;
    sw_pg_param* params;
    PGresult* result;
    int row_index;
    unsigned char* blob_cache;
    sz blob_cache_len;
} sw_pg_stmt;

static void sw_pg_close(sw_db* db);
static i32 sw_pg_exec(sw_db* db, const c8* sql);
static sw_db_stmt* sw_pg_prepare(sw_db* db, const c8* sql);
static void sw_pg_finalize(sw_db_stmt* stmt);
static i32 sw_pg_reset(sw_db_stmt* stmt);
static i32 sw_pg_bind_null(sw_db_stmt* stmt, sz index);
static i32 sw_pg_bind_int(sw_db_stmt* stmt, sz index, i64 value);
static i32 sw_pg_bind_float(sw_db_stmt* stmt, sz index, f64 value);
static i32 sw_pg_bind_text(sw_db_stmt* stmt, sz index, const c8* value);
static i32 sw_pg_bind_blob(sw_db_stmt* stmt, sz index, const void* value, sz value_len);
static sw_db_result sw_pg_step(sw_db_stmt* stmt);
static sz sw_pg_column_count(const sw_db_stmt* stmt);
static const c8* sw_pg_column_name(const sw_db_stmt* stmt, sz index);
static sw_db_value_type sw_pg_column_type(const sw_db_stmt* stmt, sz index);
static i64 sw_pg_column_int(const sw_db_stmt* stmt, sz index);
static f64 sw_pg_column_float(const sw_db_stmt* stmt, sz index);
static const c8* sw_pg_column_text(const sw_db_stmt* stmt, sz index);
static sz sw_pg_column_blob(const sw_db_stmt* stmt, sz index, const void** out_data);
static i64 sw_pg_changes(const sw_db* db);
static i64 sw_pg_last_insert_id(const sw_db* db);

static const sw_db_vtable sw_pg_ops = {
    sw_pg_close,
    sw_pg_exec,
    sw_pg_prepare,
    sw_pg_finalize,
    sw_pg_reset,
    sw_pg_bind_null,
    sw_pg_bind_int,
    sw_pg_bind_float,
    sw_pg_bind_text,
    sw_pg_bind_blob,
    sw_pg_step,
    sw_pg_column_count,
    sw_pg_column_name,
    sw_pg_column_type,
    sw_pg_column_int,
    sw_pg_column_float,
    sw_pg_column_text,
    sw_pg_column_blob,
    sw_pg_changes,
    sw_pg_last_insert_id
};

static void sw_pg_clear_result(sw_pg_stmt* pg_stmt) {
    if (pg_stmt != NULL && pg_stmt->result != NULL) {
        PQclear(pg_stmt->result);
        pg_stmt->result = NULL;
    }
    if (pg_stmt != NULL) {
        pg_stmt->row_index = -1;
    }
}

static void sw_pg_clear_blob_cache(sw_pg_stmt* pg_stmt) {
    if (pg_stmt != NULL && pg_stmt->blob_cache != NULL) {
        PQfreemem(pg_stmt->blob_cache);
        pg_stmt->blob_cache = NULL;
        pg_stmt->blob_cache_len = 0;
    }
}

static void sw_pg_param_clear(sw_pg_param* param) {
    if (param == NULL) {
        return;
    }
    free(param->data);
    memset(param, 0, sizeof(*param));
}

static b8 sw_pg_buffer_append(c8** buffer, sz* len, sz* cap, const c8* text, sz text_len) {
    c8* grown;
    sz needed;
    sz next_cap;

    if (text_len == 0) {
        return 1;
    }
    if (buffer == NULL || len == NULL || cap == NULL || text == NULL) {
        return 0;
    }
    if (*len > SIZE_MAX - text_len - 1) {
        return 0;
    }

    needed = *len + text_len + 1;
    if (needed > *cap) {
        next_cap = *cap > 0 ? *cap * 2 : 128;
        while (next_cap < needed) {
            if (next_cap > SIZE_MAX / 2) {
                next_cap = needed;
                break;
            }
            next_cap *= 2;
        }
        grown = (c8*)realloc(*buffer, next_cap);
        if (grown == NULL) {
            return 0;
        }
        *buffer = grown;
        *cap = next_cap;
    }

    memcpy(*buffer + *len, text, text_len);
    *len += text_len;
    (*buffer)[*len] = '\0';
    return 1;
}

static b8 sw_pg_buffer_append_char(c8** buffer, sz* len, sz* cap, c8 ch) {
    return sw_pg_buffer_append(buffer, len, cap, &ch, 1);
}

static sz sw_pg_dollar_quote_len(const c8* sql) {
    sz i = 1;

    if (sql == NULL || sql[0] != '$') {
        return 0;
    }
    if (sql[1] == '$') {
        return 2;
    }
    if (!(isalpha((unsigned char)sql[1]) || sql[1] == '_')) {
        return 0;
    }
    while (isalnum((unsigned char)sql[i]) || sql[i] == '_') {
        ++i;
    }
    return sql[i] == '$' ? i + 1 : 0;
}

static b8 sw_pg_copy_until(c8** out, sz* out_len, sz* out_cap, const c8* sql, sz* in_out_pos, const c8* end, sz end_len) {
    sz pos = *in_out_pos;

    while (sql[pos] != '\0') {
        if (end_len > 0 && strncmp(sql + pos, end, end_len) == 0) {
            if (!sw_pg_buffer_append(out, out_len, out_cap, end, end_len)) {
                return 0;
            }
            pos += end_len;
            *in_out_pos = pos;
            return 1;
        }
        if (!sw_pg_buffer_append_char(out, out_len, out_cap, sql[pos])) {
            return 0;
        }
        ++pos;
    }

    *in_out_pos = pos;
    return 1;
}

static b8 sw_pg_rewrite_sql(const c8* sql, c8** out_sql, sz* out_params) {
    c8* out = NULL;
    sz out_len = 0;
    sz out_cap = 0;
    sz pos = 0;
    sz params = 0;

    if (out_sql != NULL) {
        *out_sql = NULL;
    }
    if (out_params != NULL) {
        *out_params = 0;
    }
    if (sql == NULL || out_sql == NULL || out_params == NULL) {
        return 0;
    }

    while (sql[pos] != '\0') {
        if (sql[pos] == '\'' || ((sql[pos] == 'E' || sql[pos] == 'e') && sql[pos + 1] == '\'')) {
            const b8 escape_string = sql[pos] != '\'';

            if (escape_string && !sw_pg_buffer_append_char(&out, &out_len, &out_cap, sql[pos++])) {
                free(out);
                return 0;
            }
            if (!sw_pg_buffer_append_char(&out, &out_len, &out_cap, sql[pos++])) {
                free(out);
                return 0;
            }
            while (sql[pos] != '\0') {
                if (!sw_pg_buffer_append_char(&out, &out_len, &out_cap, sql[pos])) {
                    free(out);
                    return 0;
                }
                if (escape_string && sql[pos] == '\\' && sql[pos + 1] != '\0') {
                    ++pos;
                    if (!sw_pg_buffer_append_char(&out, &out_len, &out_cap, sql[pos])) {
                        free(out);
                        return 0;
                    }
                    ++pos;
                    continue;
                }
                if (sql[pos] == '\'' && sql[pos + 1] == '\'') {
                    ++pos;
                    if (!sw_pg_buffer_append_char(&out, &out_len, &out_cap, sql[pos])) {
                        free(out);
                        return 0;
                    }
                } else if (sql[pos] == '\'') {
                    ++pos;
                    break;
                }
                ++pos;
            }
            continue;
        }

        if (sql[pos] == '"') {
            if (!sw_pg_buffer_append_char(&out, &out_len, &out_cap, sql[pos++])) {
                free(out);
                return 0;
            }
            while (sql[pos] != '\0') {
                if (!sw_pg_buffer_append_char(&out, &out_len, &out_cap, sql[pos])) {
                    free(out);
                    return 0;
                }
                if (sql[pos] == '"' && sql[pos + 1] == '"') {
                    ++pos;
                    if (!sw_pg_buffer_append_char(&out, &out_len, &out_cap, sql[pos])) {
                        free(out);
                        return 0;
                    }
                } else if (sql[pos] == '"') {
                    ++pos;
                    break;
                }
                ++pos;
            }
            continue;
        }

        if (sql[pos] == '-' && sql[pos + 1] == '-') {
            if (!sw_pg_buffer_append(&out, &out_len, &out_cap, sql + pos, 2)) {
                free(out);
                return 0;
            }
            pos += 2;
            while (sql[pos] != '\0' && sql[pos] != '\n') {
                if (!sw_pg_buffer_append_char(&out, &out_len, &out_cap, sql[pos++])) {
                    free(out);
                    return 0;
                }
            }
            continue;
        }

        if (sql[pos] == '/' && sql[pos + 1] == '*') {
            if (!sw_pg_buffer_append(&out, &out_len, &out_cap, sql + pos, 2)) {
                free(out);
                return 0;
            }
            pos += 2;
            if (!sw_pg_copy_until(&out, &out_len, &out_cap, sql, &pos, "*/", 2)) {
                free(out);
                return 0;
            }
            continue;
        }

        if (sql[pos] == '$') {
            const sz quote_len = sw_pg_dollar_quote_len(sql + pos);
            if (quote_len > 0) {
                const c8* quote = sql + pos;
                if (!sw_pg_buffer_append(&out, &out_len, &out_cap, quote, quote_len)) {
                    free(out);
                    return 0;
                }
                pos += quote_len;
                if (!sw_pg_copy_until(&out, &out_len, &out_cap, sql, &pos, quote, quote_len)) {
                    free(out);
                    return 0;
                }
                continue;
            }
        }

        if (sql[pos] == '?') {
            c8 placeholder[32];
            int written;

            if (params == (sz)INT_MAX) {
                free(out);
                return 0;
            }
            ++params;
            written = snprintf(placeholder, sizeof(placeholder), "$%zu", params);
            if (written < 0 || (sz)written >= sizeof(placeholder)
                || !sw_pg_buffer_append(&out, &out_len, &out_cap, placeholder, (sz)written)) {
                free(out);
                return 0;
            }
            ++pos;
            continue;
        }

        if (!sw_pg_buffer_append_char(&out, &out_len, &out_cap, sql[pos++])) {
            free(out);
            return 0;
        }
    }

    if (out == NULL) {
        out = sw_strdup_cstr("");
        if (out == NULL) {
            return 0;
        }
    }

    *out_sql = out;
    *out_params = params;
    return 1;
}

static i64 sw_pg_cmd_tuples(PGresult* result) {
    const c8* text = result != NULL ? PQcmdTuples(result) : NULL;
    char* end = NULL;
    long long value;

    if (text == NULL || text[0] == '\0') {
        return 0;
    }

    value = strtoll(text, &end, 10);
    return end != text ? (i64)value : 0;
}

static sw_pg_db* sw_pg_db_from(sw_db* db) {
    return db != NULL ? (sw_pg_db*)db->handle : NULL;
}

static const sw_pg_db* sw_pg_const_db_from(const sw_db* db) {
    return db != NULL ? (const sw_pg_db*)db->handle : NULL;
}

static sw_pg_stmt* sw_pg_stmt_from(const sw_db_stmt* stmt) {
    return stmt != NULL ? (sw_pg_stmt*)stmt->handle : NULL;
}

static int sw_pg_column_index(const sw_db_stmt* stmt, sz index) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);
    int column_count;

    if (pg_stmt == NULL || pg_stmt->result == NULL || index > (sz)INT_MAX) {
        return -1;
    }

    column_count = PQnfields(pg_stmt->result);
    return (int)index < column_count ? (int)index : -1;
}

static int sw_pg_current_row(const sw_db_stmt* stmt) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);
    int row_count;

    if (pg_stmt == NULL || pg_stmt->result == NULL || pg_stmt->row_index < 0) {
        return -1;
    }

    row_count = PQntuples(pg_stmt->result);
    return pg_stmt->row_index < row_count ? pg_stmt->row_index : -1;
}

i32 sw_db_open_postgres(const sw_db_config* config, sw_db** out_db) {
    PGconn* connection;
    sw_pg_db* pg_db;
    sw_db* db;

    if (out_db != NULL) {
        *out_db = NULL;
    }
    if (config == NULL || out_db == NULL || config->url == NULL) {
        sw_db_set_global_error("invalid PostgreSQL configuration");
        return -1;
    }

    connection = PQconnectdb(config->url);
    if (connection == NULL) {
        sw_db_set_global_error("PostgreSQL open failed");
        return -1;
    }
    if (PQstatus(connection) != CONNECTION_OK) {
        sw_db_set_global_error("PostgreSQL open failed: %s", PQerrorMessage(connection));
        PQfinish(connection);
        return -1;
    }

    pg_db = (sw_pg_db*)calloc(1, sizeof(*pg_db));
    if (pg_db == NULL) {
        PQfinish(connection);
        sw_db_set_global_error("out of memory");
        return -1;
    }
    pg_db->connection = connection;

    db = sw_db_alloc(SW_DB_DRIVER_POSTGRES, &sw_pg_ops, config->url, pg_db);
    if (db == NULL) {
        PQfinish(connection);
        free(pg_db);
        return -1;
    }

    *out_db = db;
    return 0;
}

static void sw_pg_close(sw_db* db) {
    sw_pg_db* pg_db = sw_pg_db_from(db);

    if (pg_db == NULL) {
        return;
    }
    if (pg_db->connection != NULL) {
        PQfinish(pg_db->connection);
    }
    free(pg_db);
    db->handle = NULL;
}

static i32 sw_pg_exec(sw_db* db, const c8* sql) {
    sw_pg_db* pg_db = sw_pg_db_from(db);
    PGresult* result;
    ExecStatusType status;

    if (pg_db == NULL || pg_db->connection == NULL) {
        sw_db_set_error(db, "PostgreSQL connection is not open");
        return -1;
    }

    result = PQexec(pg_db->connection, sql);
    if (result == NULL) {
        sw_db_set_error(db, "PostgreSQL exec failed: %s", PQerrorMessage(pg_db->connection));
        return -1;
    }

    status = PQresultStatus(result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        sw_db_set_error(db, "PostgreSQL exec failed: %s", PQresultErrorMessage(result));
        PQclear(result);
        return -1;
    }

    pg_db->changes = sw_pg_cmd_tuples(result);
    PQclear(result);
    sw_db_set_error(db, NULL);
    return 0;
}

static sw_db_stmt* sw_pg_prepare(sw_db* db, const c8* sql) {
    sw_pg_stmt* pg_stmt;
    sw_db_stmt* stmt;
    c8* rewritten = NULL;
    sz param_count = 0;

    if (!sw_pg_rewrite_sql(sql, &rewritten, &param_count)) {
        sw_db_set_error(db, "PostgreSQL prepare failed while rewriting placeholders");
        return NULL;
    }

    pg_stmt = (sw_pg_stmt*)calloc(1, sizeof(*pg_stmt));
    stmt = (sw_db_stmt*)calloc(1, sizeof(*stmt));
    if (pg_stmt == NULL || stmt == NULL) {
        free(rewritten);
        free(pg_stmt);
        free(stmt);
        sw_db_set_error(db, "out of memory");
        return NULL;
    }

    if (param_count > 0) {
        pg_stmt->params = (sw_pg_param*)calloc(param_count, sizeof(*pg_stmt->params));
        if (pg_stmt->params == NULL) {
            free(rewritten);
            free(pg_stmt);
            free(stmt);
            sw_db_set_error(db, "out of memory");
            return NULL;
        }
    }

    pg_stmt->sql = rewritten;
    pg_stmt->param_count = param_count;
    pg_stmt->row_index = -1;

    stmt->db = db;
    stmt->handle = pg_stmt;
    sw_db_set_error(db, NULL);
    return stmt;
}

static void sw_pg_finalize(sw_db_stmt* stmt) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);
    sz i;

    if (pg_stmt == NULL) {
        return;
    }

    sw_pg_clear_result(pg_stmt);
    sw_pg_clear_blob_cache(pg_stmt);
    for (i = 0; i < pg_stmt->param_count; ++i) {
        sw_pg_param_clear(&pg_stmt->params[i]);
    }
    free(pg_stmt->params);
    free(pg_stmt->sql);
    free(pg_stmt);
    stmt->handle = NULL;
}

static i32 sw_pg_reset(sw_db_stmt* stmt) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);

    if (pg_stmt == NULL) {
        return -1;
    }

    sw_pg_clear_result(pg_stmt);
    sw_pg_clear_blob_cache(pg_stmt);
    return 0;
}

static sw_pg_param* sw_pg_param_at(sw_db_stmt* stmt, sz index) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);

    if (pg_stmt == NULL || index == 0 || index > pg_stmt->param_count) {
        if (stmt != NULL) {
            sw_db_set_error(stmt->db, "bind index out of range");
        }
        return NULL;
    }

    return &pg_stmt->params[index - 1];
}

static i32 sw_pg_bind_owned(sw_db_stmt* stmt, sz index, c8* data, int len, int format) {
    sw_pg_param* param = sw_pg_param_at(stmt, index);

    if (param == NULL) {
        free(data);
        return -1;
    }
    sw_pg_param_clear(param);
    param->data = data;
    param->len = len;
    param->format = format;
    return 0;
}

static i32 sw_pg_bind_null(sw_db_stmt* stmt, sz index) {
    return sw_pg_bind_owned(stmt, index, NULL, 0, 0);
}

static i32 sw_pg_set_bind_oom(sw_db_stmt* stmt) {
    if (stmt != NULL) {
        sw_db_set_error(stmt->db, "out of memory");
    }
    return -1;
}

static i32 sw_pg_bind_int(sw_db_stmt* stmt, sz index, i64 value) {
    c8 buffer[64];
    c8* copy;
    int len;

    len = snprintf(buffer, sizeof(buffer), "%lld", (long long)value);
    if (len < 0 || (sz)len >= sizeof(buffer)) {
        return -1;
    }
    copy = sw_strdup_cstr(buffer);
    return copy != NULL ? sw_pg_bind_owned(stmt, index, copy, len, 0) : sw_pg_set_bind_oom(stmt);
}

static i32 sw_pg_bind_float(sw_db_stmt* stmt, sz index, f64 value) {
    c8 buffer[128];
    c8* copy;
    int len;

    len = snprintf(buffer, sizeof(buffer), "%.17g", value);
    if (len < 0 || (sz)len >= sizeof(buffer)) {
        return -1;
    }
    copy = sw_strdup_cstr(buffer);
    return copy != NULL ? sw_pg_bind_owned(stmt, index, copy, len, 0) : sw_pg_set_bind_oom(stmt);
}

static i32 sw_pg_bind_text(sw_db_stmt* stmt, sz index, const c8* value) {
    c8* copy;

    if (value == NULL) {
        return sw_pg_bind_null(stmt, index);
    }

    copy = sw_strdup_cstr(value);
    return copy != NULL ? sw_pg_bind_owned(stmt, index, copy, (int)strlen(copy), 0) : sw_pg_set_bind_oom(stmt);
}

static i32 sw_pg_bind_blob(sw_db_stmt* stmt, sz index, const void* value, sz value_len) {
    c8* copy;

    if (value == NULL) {
        return sw_pg_bind_null(stmt, index);
    }
    if (value_len > (sz)INT_MAX) {
        sw_db_set_error(stmt->db, "blob value is too large");
        return -1;
    }

    copy = (c8*)malloc(value_len > 0 ? value_len : 1);
    if (copy == NULL) {
        return sw_pg_set_bind_oom(stmt);
    }
    if (value_len > 0) {
        memcpy(copy, value, value_len);
    }
    return sw_pg_bind_owned(stmt, index, copy, (int)value_len, 1);
}

static b8 sw_pg_execute_stmt(sw_db_stmt* stmt) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);
    sw_pg_db* pg_db = sw_pg_db_from(stmt != NULL ? stmt->db : NULL);
    const char** values = NULL;
    int* lengths = NULL;
    int* formats = NULL;
    ExecStatusType status;
    sz i;

    if (pg_stmt == NULL || pg_db == NULL || pg_db->connection == NULL) {
        return 0;
    }
    if (pg_stmt->param_count > (sz)INT_MAX) {
        sw_db_set_error(stmt->db, "too many parameters");
        return 0;
    }
    if (pg_stmt->param_count > 0) {
        values = (const char**)calloc(pg_stmt->param_count, sizeof(*values));
        lengths = (int*)calloc(pg_stmt->param_count, sizeof(*lengths));
        formats = (int*)calloc(pg_stmt->param_count, sizeof(*formats));
        if (values == NULL || lengths == NULL || formats == NULL) {
            free(values);
            free(lengths);
            free(formats);
            sw_db_set_error(stmt->db, "out of memory");
            return 0;
        }

        for (i = 0; i < pg_stmt->param_count; ++i) {
            values[i] = pg_stmt->params[i].data;
            lengths[i] = pg_stmt->params[i].len;
            formats[i] = pg_stmt->params[i].format;
        }
    }

    sw_pg_clear_result(pg_stmt);
    sw_pg_clear_blob_cache(pg_stmt);
    pg_stmt->result = PQexecParams(
        pg_db->connection,
        pg_stmt->sql,
        (int)pg_stmt->param_count,
        NULL,
        values,
        lengths,
        formats,
        0
    );

    free(values);
    free(lengths);
    free(formats);

    if (pg_stmt->result == NULL) {
        sw_db_set_error(stmt->db, "PostgreSQL step failed: %s", PQerrorMessage(pg_db->connection));
        return 0;
    }

    status = PQresultStatus(pg_stmt->result);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        sw_db_set_error(stmt->db, "PostgreSQL step failed: %s", PQresultErrorMessage(pg_stmt->result));
        sw_pg_clear_result(pg_stmt);
        return 0;
    }

    pg_db->changes = sw_pg_cmd_tuples(pg_stmt->result);
    pg_stmt->row_index = -1;
    sw_db_set_error(stmt->db, NULL);
    return 1;
}

static sw_db_result sw_pg_step(sw_db_stmt* stmt) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);
    ExecStatusType status;
    int row_count;

    if (pg_stmt == NULL) {
        return SW_DB_ERROR;
    }
    if (pg_stmt->result == NULL && !sw_pg_execute_stmt(stmt)) {
        return SW_DB_ERROR;
    }

    status = PQresultStatus(pg_stmt->result);
    if (status != PGRES_TUPLES_OK) {
        return SW_DB_DONE;
    }

    row_count = PQntuples(pg_stmt->result);
    if (pg_stmt->row_index + 1 < row_count) {
        pg_stmt->row_index += 1;
        sw_pg_clear_blob_cache(pg_stmt);
        return SW_DB_ROW;
    }

    return SW_DB_DONE;
}

static sz sw_pg_column_count(const sw_db_stmt* stmt) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);
    return pg_stmt != NULL && pg_stmt->result != NULL ? (sz)PQnfields(pg_stmt->result) : 0;
}

static const c8* sw_pg_column_name(const sw_db_stmt* stmt, sz index) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);
    const int column_index = sw_pg_column_index(stmt, index);

    return pg_stmt != NULL && column_index >= 0 ? PQfname(pg_stmt->result, column_index) : NULL;
}

static sw_db_value_type sw_pg_column_type(const sw_db_stmt* stmt, sz index) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);
    const int row = sw_pg_current_row(stmt);
    const int column_index = sw_pg_column_index(stmt, index);
    Oid oid;

    if (pg_stmt == NULL || row < 0 || column_index < 0 || PQgetisnull(pg_stmt->result, row, column_index)) {
        return SW_DB_VALUE_NULL;
    }

    oid = PQftype(pg_stmt->result, column_index);
    switch (oid) {
        case SW_PG_INT2_OID:
        case SW_PG_INT4_OID:
        case SW_PG_INT8_OID:
            return SW_DB_VALUE_INTEGER;
        case SW_PG_FLOAT4_OID:
        case SW_PG_FLOAT8_OID:
        case SW_PG_NUMERIC_OID:
            return SW_DB_VALUE_FLOAT;
        case SW_PG_BYTEA_OID:
            return SW_DB_VALUE_BLOB;
        default:
            return SW_DB_VALUE_TEXT;
    }
}

static i64 sw_pg_column_int(const sw_db_stmt* stmt, sz index) {
    const c8* text = sw_pg_column_text(stmt, index);
    return text != NULL ? (i64)strtoll(text, NULL, 10) : 0;
}

static f64 sw_pg_column_float(const sw_db_stmt* stmt, sz index) {
    const c8* text = sw_pg_column_text(stmt, index);
    return text != NULL ? strtod(text, NULL) : 0.0;
}

static const c8* sw_pg_column_text(const sw_db_stmt* stmt, sz index) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);
    const int row = sw_pg_current_row(stmt);
    const int column_index = sw_pg_column_index(stmt, index);

    if (pg_stmt == NULL || row < 0 || column_index < 0 || PQgetisnull(pg_stmt->result, row, column_index)) {
        return NULL;
    }

    return PQgetvalue(pg_stmt->result, row, column_index);
}

static sz sw_pg_column_blob(const sw_db_stmt* stmt, sz index, const void** out_data) {
    sw_pg_stmt* pg_stmt = sw_pg_stmt_from(stmt);
    const int row = sw_pg_current_row(stmt);
    const int column_index = sw_pg_column_index(stmt, index);
    size_t len = 0;

    if (out_data != NULL) {
        *out_data = NULL;
    }
    if (pg_stmt == NULL || row < 0 || column_index < 0 || PQgetisnull(pg_stmt->result, row, column_index)) {
        return 0;
    }

    sw_pg_clear_blob_cache(pg_stmt);
    if (PQftype(pg_stmt->result, column_index) == SW_PG_BYTEA_OID) {
        pg_stmt->blob_cache = PQunescapeBytea((const unsigned char*)PQgetvalue(pg_stmt->result, row, column_index), &len);
        pg_stmt->blob_cache_len = (sz)len;
        if (out_data != NULL) {
            *out_data = pg_stmt->blob_cache;
        }
        return pg_stmt->blob_cache_len;
    }

    if (out_data != NULL) {
        *out_data = PQgetvalue(pg_stmt->result, row, column_index);
    }
    return (sz)PQgetlength(pg_stmt->result, row, column_index);
}

static i64 sw_pg_changes(const sw_db* db) {
    const sw_pg_db* pg_db = sw_pg_const_db_from(db);
    return pg_db != NULL ? pg_db->changes : 0;
}

static i64 sw_pg_last_insert_id(const sw_db* db) {
    (void)db;
    return 0;
}

#else

i32 sw_db_open_postgres(const sw_db_config* config, sw_db** out_db) {
    (void)config;
    if (out_db != NULL) {
        *out_db = NULL;
    }
    sw_db_set_global_error("PostgreSQL support is not enabled");
    return -1;
}

#endif
