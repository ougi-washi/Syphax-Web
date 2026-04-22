#ifndef SW_TRANSLATOR_H
#define SW_TRANSLATOR_H

#include "sw_export.h"
#include "syphax/s_types.h"
#include "syphax/s_array.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sw_language {
    const c8* code;
    const c8* label;
    const c8* direction;
} sw_language;
typedef s_array(sw_language, sw_languages);
typedef struct sw_translator sw_translator;

SW_API void sw_add_language_internal(sw_translator* translator, const sw_language* language);
#define sw_add_language(translator, ...) sw_add_language_internal(translator, &(sw_language){__VA_ARGS__})

SW_API sw_translator* sw_translator_create(void);
SW_API void sw_translator_destroy(sw_translator* translator);
SW_API b8 sw_translator_load_json_text(sw_translator* translator, const c8* lang, const c8* json_text);
SW_API b8 sw_translator_load_json_file(sw_translator* translator, const c8* lang, const c8* path);
SW_API b8 sw_translator_load_catalog_json_text(sw_translator* translator, const c8* lang, const c8* json_text);
SW_API b8 sw_translator_load_catalog_json_file(sw_translator* translator, const c8* lang, const c8* path);
SW_API b8 sw_translator_load_catalog_all_json_text(sw_translator* translator, const c8* json_text);
SW_API b8 sw_translator_load_catalog_all_json_file(sw_translator* translator, const c8* path);
SW_API b8 sw_translator_set_language(sw_translator* translator, const c8* lang);
SW_API const c8* sw_translator_get_language(const sw_translator* translator);
SW_API const c8* sw_translate(const sw_translator* translator, const c8* str);

#ifdef __cplusplus
}
#endif

#endif
