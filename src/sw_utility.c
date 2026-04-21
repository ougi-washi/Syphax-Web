#include "sw_internal.h"

#ifdef _WIN32
static INIT_ONCE sw_random_once = INIT_ONCE_STATIC_INIT;

static BOOL CALLBACK sw_seed_random_once(PINIT_ONCE init_once, PVOID parameter, PVOID* context) {
    (void)init_once;
    (void)parameter;
    (void)context;
    srand((unsigned int) time(NULL));
    return TRUE;
}
#else
static b8 sw_random_seeded = 0;
#endif

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

void sw_char_array_init(sw_char_array* array) {
    s_array_init(array);
}

void sw_char_array_free(sw_char_array* array) {
    s_array_clear(array);
}

void sw_char_array_reset(sw_char_array* array) {
    array->b.size = 0;
    if (array->b.data != NULL) {
        ((c8*)array->b.data)[0] = '\0';
    }
}

static b8 sw_char_array_reserve_extra(sw_char_array* array, sz extra) {
    s_array_reserve(array, array->b.size + extra + 1);
    return 1;
}

b8 sw_char_array_append_byte(sw_char_array* array, c8 value) {
    if (!sw_char_array_reserve_extra(array, 1)) {
        return 0;
    }
    ((c8*)array->b.data)[array->b.size++] = value;
    ((c8*)array->b.data)[array->b.size] = '\0';
    return 1;
}

b8 sw_char_array_append_bytes(sw_char_array* array, const void* data, sz len) {
    if (len == 0) {
        if (array->b.data == NULL) {
            sw_char_array_reserve_extra(array, 0);
        }
        return 1;
    }
    if (data == NULL) {
        return 0;
    }
    sw_char_array_reserve_extra(array, len);
    memcpy((c8*)array->b.data + array->b.size, data, len);
    array->b.size += len;
    ((c8*)array->b.data)[array->b.size] = '\0';
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
    int needed;

    va_copy(count_args, ap);
    needed = vsnprintf(NULL, 0, fmt, count_args);
    va_end(count_args);

    if (needed < 0) {
        return 0;
    }

    sw_char_array_reserve_extra(array, (sz)needed);

    va_copy(write_args, ap);
    vsnprintf((c8*)array->b.data + array->b.size, (sz)needed + 1, fmt, write_args);
    va_end(write_args);

    array->b.size += (sz)needed;
    return 1;
}

void sw_char_array_consume_prefix(sw_char_array* array, sz count) {
    if (count >= array->b.size) {
        sw_char_array_reset(array);
        return;
    }
    memmove(array->b.data, (c8*)array->b.data + count, array->b.size - count);
    array->b.size -= count;
    ((c8*)array->b.data)[array->b.size] = '\0';
}

const c8* sw_char_array_data(const sw_char_array* array) {
    if (array->b.data == NULL) {
        return "";
    }
    return (const c8*)array->b.data;
}

sz sw_char_array_size(const sw_char_array* array) {
    return array->b.size;
}

f64 sw_get_time(void) {
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

c8* sw_get_file_content(const c8* file_path, sz* buffer_size) {
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
    if (size < 0) {
        fclose(file);
        return NULL;
    }
    rewind(file);

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

    content[size] = '\0';
    fclose(file);

    if (buffer_size != NULL) {
        *buffer_size = (sz)size;
    }

    return content;
}

b8 sw_generate_unique_filename(const c8* original_name, c8* new_name, sz max_len) {
    const c8* ext;
    time_t raw_time;
    struct tm time_info;
    int random_num;

    if (new_name == NULL || max_len == 0) {
        return 0;
    }

    raw_time = time(NULL);
#ifdef _WIN32
    localtime_s(&time_info, &raw_time);
    InitOnceExecuteOnce(&sw_random_once, sw_seed_random_once, NULL, NULL);
#else
    localtime_r(&raw_time, &time_info);
    if (!sw_random_seeded) {
        sw_random_seeded = 1;
        srand((unsigned int)raw_time);
    }
#endif

    ext = (original_name != NULL) ? strrchr(original_name, '.') : NULL;
    if (ext == NULL) {
        ext = "";
    }

    random_num = rand() % 10000;

    return snprintf(new_name, max_len, "file_%04d%02d%02d_%02d%02d%02d_%04d%s",
        time_info.tm_year + 1900,
        time_info.tm_mon + 1,
        time_info.tm_mday,
        time_info.tm_hour,
        time_info.tm_min,
        time_info.tm_sec,
        random_num,
        ext) > 0;
}

u32 sw_random(u32 min, u32 max) {
    s_assertf(min <= max, "sw_random :: min must be <= max\n");
#ifdef _WIN32
    InitOnceExecuteOnce(&sw_random_once, sw_seed_random_once, NULL, NULL);
#else
    if (!sw_random_seeded) {
        sw_random_seeded = 1;
        srand((unsigned int) time(NULL));
    }
#endif
    return min + (u32)(rand() % (int)(max - min + 1));
}

b8 sw_hash(const c8* str, c8* hash, sz hash_len) {
    u32 hash_value = 5381u;
    sz i;

    if (str == NULL || hash == NULL || hash_len == 0) {
        return 0;
    }

    for (i = 0; str[i] != '\0'; ++i) {
        hash_value = ((hash_value << 5u) + hash_value) ^ (u32)(unsigned char)str[i];
    }

    return snprintf(hash, hash_len, "%u", hash_value) > 0;
}
