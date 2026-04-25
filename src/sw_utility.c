#include "sw_utility.h"
#include "sw_internal.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

char* sw_strdup_cstr(const c8* str) {
    if (str == NULL) {
        return NULL;
    }
    return sw_strdup_range(str, strlen(str));
}

char* sw_strdup_range(const c8* str, sz len) {
    char* out = (char*)malloc(len + 1);
    if (out == NULL) {
        return NULL;
    }
    if (len > 0) {
        memcpy(out, str, len);
    }
    out[len] = '\0';
    return out;
}

int sw_stricmp_ascii(const c8* lhs, const c8* rhs) {
    if (lhs == NULL && rhs == NULL) {
        return 0;
    }
    if (lhs == NULL) {
        return -1;
    }
    if (rhs == NULL) {
        return 1;
    }

    while (*lhs != '\0' && *rhs != '\0') {
        const int lhs_ch = tolower((unsigned char)*lhs);
        const int rhs_ch = tolower((unsigned char)*rhs);
        if (lhs_ch != rhs_ch) {
            return lhs_ch - rhs_ch;
        }
        ++lhs;
        ++rhs;
    }
    return (int)((unsigned char)*lhs) - (int)((unsigned char)*rhs);
}

const c8* sw_strcasestr_ascii(const c8* haystack, const c8* needle) {
    sz needle_len;
    sz haystack_len;
    sz i;
    sz j;

    if (haystack == NULL || needle == NULL) {
        return NULL;
    }

    needle_len = strlen(needle);
    haystack_len = strlen(haystack);

    if (needle_len == 0) {
        return haystack;
    }

    if (needle_len > haystack_len) {
        return NULL;
    }

    for (i = 0; i + needle_len <= haystack_len; ++i) {
        for (j = 0; j < needle_len; ++j) {
            if (tolower((unsigned char)haystack[i + j]) != tolower((unsigned char)needle[j])) {
                break;
            }
        }
        if (j == needle_len) {
            return haystack + i;
        }
    }

    return NULL;
}

b8 sw_matches_query(const c8* text, const c8* query, b8 case_sensitive) {
    sz query_len;
    sz text_len;
    sz i;
    sz j;

    if (text == NULL || query == NULL) {
        return 0;
    }

    query_len = strlen(query);
    text_len = strlen(text);

    if (query_len == 0) {
        return 1;
    }
    if (query_len > text_len) {
        return 0;
    }

    for (i = 0; i + query_len <= text_len; ++i) {
        for (j = 0; j < query_len; ++j) {
            c8 text_ch = text[i + j];
            c8 query_ch = query[j];

            if (!case_sensitive) {
                text_ch = (c8)tolower((unsigned char)text_ch);
                query_ch = (c8)tolower((unsigned char)query_ch);
            }

            if (text_ch != query_ch) {
                break;
            }
        }
        if (j == query_len) {
            return 1;
        }
    }

    return 0;
}

static sz sw_char_array_content_size(const sw_char_array* array) {
    const sz size = s_array_get_size(array);
    return size > 0 ? size - 1 : 0;
}

static c8* sw_char_array_terminator(sw_char_array* array) {
    const sz size = s_array_get_size(array);

    if (size == 0) {
        return NULL;
    }

    return s_array_get(array, s_array_handle(array, (u32)(size - 1)));
}

void sw_char_array_init(sw_char_array* array) {
    s_array_init(array);
}

void sw_char_array_free(sw_char_array* array) {
    s_array_clear(array);
}

void sw_char_array_reset(sw_char_array* array) {
    const sz capacity = s_array_get_capacity(array);

    s_array_clear(array);
    if (capacity > 0) {
        s_array_reserve(array, capacity);
    }
}

static b8 sw_char_array_reserve_extra(sw_char_array* array, sz extra) {
    s_array_reserve(array, sw_char_array_content_size(array) + extra + 1);
    return 1;
}

b8 sw_char_array_append_byte(sw_char_array* array, c8 value) {
    c8* terminator;
    const c8 nul = '\0';

    if (!sw_char_array_reserve_extra(array, 1)) {
        return 0;
    }

    terminator = sw_char_array_terminator(array);
    if (terminator != NULL) {
        *terminator = value;
    } else {
        s_array_add(array, value);
    }

    s_array_add(array, nul);
    return 1;
}

b8 sw_char_array_append_bytes(sw_char_array* array, const void* data, sz len) {
    const c8* bytes = (const c8*)data;
    c8* terminator;
    const c8 nul = '\0';
    sz i = 0;

    if (len == 0) {
        return 1;
    }
    if (data == NULL) {
        return 0;
    }

    sw_char_array_reserve_extra(array, len);

    terminator = sw_char_array_terminator(array);
    if (terminator != NULL) {
        *terminator = bytes[0];
        i = 1;
    }

    for (; i < len; ++i) {
        s_array_add(array, bytes[i]);
    }

    s_array_add(array, nul);
    return 1;
}

b8 sw_char_array_append_cstr(sw_char_array* array, const c8* str) {
    if (str == NULL) {
        return 1;
    }
    return sw_char_array_append_bytes(array, str, strlen(str));
}

b8 sw_char_array_append_vformat(sw_char_array* array, const c8* fmt, va_list ap) {
    va_list count_args;
    va_list write_args;
    char* buffer;
    b8 ok;
    int needed;

    va_copy(count_args, ap);
    needed = vsnprintf(NULL, 0, fmt, count_args);
    va_end(count_args);

    if (needed < 0) {
        return 0;
    }

    buffer = (char*)malloc((sz)needed + 1);
    if (buffer == NULL) {
        return 0;
    }

    va_copy(write_args, ap);
    vsnprintf(buffer, (sz)needed + 1, fmt, write_args);
    va_end(write_args);

    ok = sw_char_array_append_bytes(array, buffer, (sz)needed);
    free(buffer);
    return ok;
}

void sw_char_array_consume_prefix(sw_char_array* array, sz count) {
    sw_char_array tail;
    const c8* data;
    const sz size = sw_char_array_content_size(array);

    if (count == 0) {
        return;
    }
    if (count >= size) {
        sw_char_array_reset(array);
        return;
    }

    data = sw_char_array_data(array);
    s_array_init(&tail);
    if (!sw_char_array_append_bytes(&tail, data + count, size - count)) {
        sw_char_array_free(&tail);
        return;
    }

    s_array_copy(array, &tail);
    sw_char_array_free(&tail);
}

const c8* sw_char_array_data(const sw_char_array* array) {
    if (s_array_get_size(array) == 0) {
        return "";
    }
    return (const c8*)s_array_get_data((sw_char_array*)array);
}

sz sw_char_array_size(const sw_char_array* array) {
    return sw_char_array_content_size(array);
}

f64 sw_now_ms(void) {
#ifdef _WIN32
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return ((f64)counter.QuadPart * 1000.0) / (f64)frequency.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((f64)ts.tv_sec * 1000.0) + ((f64)ts.tv_nsec / 1000000.0);
#endif
}

c8* sw_read_file(const c8* file_path, sz* buffer_size) {
    FILE* file;
    long size;
    c8* content;

    if (buffer_size != NULL) {
        *buffer_size = 0;
    }
    if (file_path == NULL) {
        return NULL;
    }

    file = fopen(file_path, "rb");
    if (file == NULL) {
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }

    size = ftell(file);
    if (size < 0 || (unsigned long)size > (unsigned long)(SIZE_MAX - 1)) {
        fclose(file);
        return NULL;
    }
    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }

    content = (c8*)malloc((sz)size + 1);
    if (content == NULL) {
        fclose(file);
        return NULL;
    }

    if ((sz)size > 0 && fread(content, 1, (sz)size, file) != (sz)size) {
        fclose(file);
        free(content);
        return NULL;
    }

    content[(sz)size] = '\0';
    fclose(file);

    if (buffer_size != NULL) {
        *buffer_size = (sz)size;
    }

    return content;
}
