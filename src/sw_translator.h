// Syphax-Web - Ougi Washi

#ifndef SW_TRANSLATOR_H
#define SW_TRANSLATOR_H

#include "sw_types.h"

#define SW_TRANSLATIONS_MAX 1024
#define SW_LANGUAGES_MAX 3

static c8* language[SW_LANGUAGES_MAX] = {
    "en",
    "ar",
    "fr"
};

extern void sw_set_language(c8* lang);
extern const c8* sw_translate(const c8* str);

#endif // SW_TRANSLATOR_H
