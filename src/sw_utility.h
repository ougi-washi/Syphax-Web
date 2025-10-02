// Syphax-Web - Ougi Washi

#ifndef SW_UTILITY_H
#define SW_UTILITY_H

#include "sw_types.h"
#include "sw_array.h"
#include <stdarg.h>

#define SW_REQUEST_PARAM_NAME_MAX_LEN 128
#define SW_REQUEST_PARAM_VALUE_MAX_LEN 1024
#define SW_REQUEST_PARAMS_MAX_LEN 64

typedef struct {
    c8 name[SW_REQUEST_PARAM_NAME_MAX_LEN];
    c8 value[SW_REQUEST_PARAM_VALUE_MAX_LEN];
} sw_request_param;
SW_DEFINE_ARRAY(sw_request_param, sw_request_params, SW_REQUEST_PARAMS_MAX_LEN);

// Get current time in milliseconds
extern f64 sw_get_time(); 
extern c8* sw_get_file_content(c8* file_path, sz* buffer_size);
extern void generate_unique_filename(const char* original_name, char* new_name, size_t max_len);
extern u32 sw_random(u32 min, u32 max);
extern void sw_hash(const c8* str, c8* hash);

// Strings
#define SW_MAX_FORMATTED_STRING_SIZE 1024
static void sw_append(c8 *output, const c8 *fmt, ...) {
    va_list ap;
    char tmp[SW_MAX_FORMATTED_STRING_SIZE];
    va_start(ap, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, ap);
    va_end(ap);
    strcat(output, tmp);
}
#define SW_FORMATTED_STR_MAX_LEN 1024
#define sw_make_str(_var_name, _str, ...) \
    c8 _var_name[sw_FORMATTED_STR_MAX_LEN] = {0}; \
    sw_append(_var_name, _str, ##__VA_ARGS__) 


#endif // sw_UTILITY_H
