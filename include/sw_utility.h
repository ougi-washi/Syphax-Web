#ifndef SW_UTILITY_H
#define SW_UTILITY_H

#include "sw_export.h"
#include "syphax/s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

SW_API f64 sw_get_time(void);
SW_API c8* sw_get_file_content(const c8* file_path, sz* buffer_size);
SW_API b8 sw_generate_unique_filename(const c8* original_name, c8* new_name, sz max_len);
SW_API u32 sw_random(u32 min, u32 max);
SW_API b8 sw_hash(const c8* str, c8* hash, sz hash_len);

#ifdef __cplusplus
}
#endif

#endif
