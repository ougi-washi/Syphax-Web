#ifndef SW_TRANSLATOR_H
#define SW_TRANSLATOR_H

#include "syphax/web/sw_export.h"
#include "syphax/web/sw_types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SW_LANGUAGES_MAX 3

typedef struct sw_translator sw_translator;

SW_API sw_translator* sw_translator_create(void);
SW_API void sw_translator_destroy(sw_translator* translator);
SW_API b8 sw_translator_set_language(sw_translator* translator, const c8* lang);
SW_API const c8* sw_translator_get_language(const sw_translator* translator);
SW_API const c8* sw_translate(const sw_translator* translator, const c8* str);

#ifdef __cplusplus
}
#endif

#endif
