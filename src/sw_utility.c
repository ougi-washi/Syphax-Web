// Syphax-Web - Ougi Washi

#include "sw_utility.h"
#include "sw_time.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

f64 sw_get_time(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

c8* sw_get_file_content(c8* file_path, sz* buffer_size) {
    FILE* file = fopen(file_path, "r");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    *buffer_size = ftell(file);
    rewind(file);
    c8* file_content = (c8*)malloc(*buffer_size + 1);
    fread(file_content, 1, *buffer_size, file);
    file_content[*buffer_size] = '\0';
    fclose(file);
    return file_content;
}

void generate_unique_filename(const char* original_name, char* new_name, size_t max_len) {
    time_t t = time(NULL);
    struct tm tm = *localtime(&t);
    
    // Extract file extension
    const char* ext = strrchr(original_name, '.');
    if (ext == NULL) {
        ext = "";
    }
    
    // Generate filename with timestamp and random number
    srand(time(NULL));
    int random_num = rand() % 10000;
    
    snprintf(new_name, max_len, "file_%04d%02d%02d_%02d%02d%02d_%04d%s",
             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
             tm.tm_hour, tm.tm_min, tm.tm_sec, random_num, ext);
}

u32 sw_random(u32 min, u32 max) {
    return rand() % (max - min + 1) + min;
}

void sw_hash(const c8* str, c8* hash) {
    sw_assertf(str, "sw_hash :: Failed to hash string, str is NULL");
    sw_assertf(hash, "sw_hash :: Failed to hash string, hash is NULL");
    
    u32 hash_value = 5381;
    for (int i = 0; i < strlen(str); i++) {
        hash_value = ((hash_value << 5) + hash_value) ^ str[i];
    }
    sprintf(hash, "%u", hash_value);
}

