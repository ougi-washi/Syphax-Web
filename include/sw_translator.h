#ifndef SW_TRANSLATOR_H
#define SW_TRANSLATOR_H

#include "sw_export.h"
#include "syphax/s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sw_translator sw_translator;

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
