#include "sw_translator.h"
#include "sw_utility.h"
#include "sw_internal.h"
#include "syphax/s_json.h"

#include <stdlib.h>
#include <string.h>

static void sw_translation_language_dispose(sw_translation_language* language);

static void sw_translation_items_free(sw_translation_item_array* items) {
    sz i;
    sw_translation_item* data;

    if (items == NULL) {
        return;
    }

    data = (sw_translation_item*)items->b.data;
    for (i = 0; i < items->b.size; ++i) {
        free(data[i].key);
        free(data[i].value);
    }

    s_array_clear(items);
}

static void sw_translation_languages_free(sw_translation_language_array* languages) {
    sz i;
    sw_translation_language* data;

    if (languages == NULL) {
        return;
    }

    data = (sw_translation_language*)languages->b.data;
    for (i = 0; i < languages->b.size; ++i) {
        sw_translation_language_dispose(&data[i]);
    }

    s_array_clear(languages);
}

static void sw_translation_language_dispose(sw_translation_language* language) {
    if (language == NULL) {
        return;
    }

    free(language->code);
    sw_translation_items_free(&language->items);
    language->code = NULL;
}

static sw_translation_language* sw_translator_find_language(
    sw_translator* translator,
    const c8* lang,
    s_handle* out_handle
) {
    sz i;
    sw_translation_language* data;

    if (out_handle != NULL) {
        *out_handle = S_HANDLE_NULL;
    }
    if (translator == NULL || lang == NULL) {
        return NULL;
    }

    data = (sw_translation_language*)translator->languages.b.data;
    for (i = 0; i < translator->languages.b.size; ++i) {
        if (data[i].code != NULL && strcmp(data[i].code, lang) == 0) {
            if (out_handle != NULL) {
                *out_handle = s_array_handle(&translator->languages, (u32)i);
            }
            return &data[i];
        }
    }

    return NULL;
}

static const sw_translation_language* sw_translator_current_language(const sw_translator* translator) {
    sw_translation_language_array* languages;

    if (translator == NULL || translator->current_language == S_HANDLE_NULL) {
        return NULL;
    }

    languages = (sw_translation_language_array*)&translator->languages;
    return s_array_get(languages, translator->current_language);
}

static b8 sw_translation_items_has_key(const sw_translation_item_array* items, const c8* key) {
    sz i;
    sw_translation_item* data;

    if (items == NULL || key == NULL) {
        return 0;
    }

    data = (sw_translation_item*)items->b.data;
    for (i = 0; i < items->b.size; ++i) {
        if (strcmp(data[i].key, key) == 0) {
            return 1;
        }
    }

    return 0;
}

static b8 sw_translation_languages_has_code(const sw_translation_language_array* languages, const c8* code) {
    sz i;
    sw_translation_language* data;

    if (languages == NULL || code == NULL) {
        return 0;
    }

    data = (sw_translation_language*)languages->b.data;
    for (i = 0; i < languages->b.size; ++i) {
        if (data[i].code != NULL && strcmp(data[i].code, code) == 0) {
            return 1;
        }
    }

    return 0;
}

static b8 sw_translation_items_add(sw_translation_item_array* items, const c8* key, const c8* value) {
    sw_translation_item item;

    if (items == NULL || key == NULL || value == NULL) {
        return 0;
    }
    if (sw_translation_items_has_key(items, key)) {
        return 0;
    }

    item.key = sw_strdup_cstr(key);
    item.value = sw_strdup_cstr(value);
    if (item.key == NULL || item.value == NULL) {
        free(item.key);
        free(item.value);
        return 0;
    }

    s_array_add(items, item);
    return 1;
}

static b8 sw_json_object_name_seen_before(const s_json* object, sz index, const c8* name) {
    sz i;

    if (object == NULL || object->type != S_JSON_OBJECT || name == NULL) {
        return 0;
    }

    for (i = 0; i < index; ++i) {
        const s_json* child = object->as.children.items[i];

        if (child != NULL && child->name != NULL && strcmp(child->name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

static b8 sw_translator_language_begin(sw_translation_language* language, const c8* lang) {
    if (language == NULL || lang == NULL) {
        return 0;
    }

    memset(language, 0, sizeof(*language));
    s_array_init(&language->items);

    language->code = sw_strdup_cstr(lang);
    if (language->code == NULL) {
        return 0;
    }

    return 1;
}

static b8 sw_translator_build_empty_language(sw_translation_language* language, const c8* lang) {
    if (language == NULL || lang == NULL) {
        return 0;
    }

    return sw_translator_language_begin(language, lang);
}

static b8 sw_translator_build_flat_language(sw_translation_language* language, const c8* lang, const s_json* root) {
    sz i;

    if (language == NULL || lang == NULL || root == NULL || root->type != S_JSON_OBJECT) {
        return 0;
    }

    if (!sw_translator_language_begin(language, lang)) {
        return 0;
    }

    for (i = 0; i < root->as.children.count; ++i) {
        const s_json* child = root->as.children.items[i];

        if (child == NULL
            || child->name == NULL
            || child->type != S_JSON_STRING
            || child->as.string == NULL
            || sw_json_object_name_seen_before(root, i, child->name)) {
            sw_translation_language_dispose(language);
            return 0;
        }

        if (!sw_translation_items_add(&language->items, child->name, child->as.string)) {
            sw_translation_language_dispose(language);
            return 0;
        }
    }

    return 1;
}

static b8 sw_translator_build_catalog_language(sw_translation_language* language, const c8* lang, const s_json* root) {
    sz i;

    if (language == NULL || lang == NULL || root == NULL || root->type != S_JSON_OBJECT) {
        return 0;
    }

    if (!sw_translator_language_begin(language, lang)) {
        return 0;
    }

    for (i = 0; i < root->as.children.count; ++i) {
        const s_json* child = root->as.children.items[i];
        sz j;
        const c8* value = NULL;

        if (child == NULL
            || child->name == NULL
            || child->type != S_JSON_OBJECT
            || sw_json_object_name_seen_before(root, i, child->name)) {
            sw_translation_language_dispose(language);
            return 0;
        }

        for (j = 0; j < child->as.children.count; ++j) {
            const s_json* locale = child->as.children.items[j];

            if (locale == NULL
                || locale->name == NULL
                || locale->type != S_JSON_STRING
                || locale->as.string == NULL
                || sw_json_object_name_seen_before(child, j, locale->name)) {
                sw_translation_language_dispose(language);
                return 0;
            }

            if (strcmp(locale->name, lang) == 0) {
                value = locale->as.string;
            }
        }

        if (value != NULL && !sw_translation_items_add(&language->items, child->name, value)) {
            sw_translation_language_dispose(language);
            return 0;
        }
    }

    return 1;
}

static b8 sw_translator_build_language_from_text(
    sw_translation_language* language,
    const c8* lang,
    const c8* json_text,
    b8 use_catalog
) {
    s_json_error error = {0};
    s_json* root;
    b8 ok;

    if (language == NULL || lang == NULL || json_text == NULL) {
        return 0;
    }

    root = s_json_parse_with_error(json_text, &error);
    if (root == NULL) {
        return 0;
    }

    ok = use_catalog
        ? sw_translator_build_catalog_language(language, lang, root)
        : sw_translator_build_flat_language(language, lang, root);

    s_json_free(root);
    return ok;
}

static b8 sw_translator_store_language(sw_translator* translator, sw_translation_language* loaded) {
    sw_translation_language* existing;
    s_handle existing_handle = S_HANDLE_NULL;

    if (translator == NULL || loaded == NULL || loaded->code == NULL) {
        return 0;
    }

    existing = sw_translator_find_language(translator, loaded->code, &existing_handle);
    if (existing != NULL) {
        sw_translation_language_dispose(existing);
        *existing = *loaded;
        if (translator->current_language == existing_handle) {
            translator->current_language = existing_handle;
        }
        memset(loaded, 0, sizeof(*loaded));
        return 1;
    }

    s_array_add(&translator->languages, *loaded);
    memset(loaded, 0, sizeof(*loaded));
    return 1;
}

static b8 sw_translator_load_text(sw_translator* translator, const c8* lang, const c8* json_text, b8 use_catalog) {
    sw_translation_language loaded;

    if (translator == NULL || lang == NULL || json_text == NULL) {
        return 0;
    }

    if (!sw_translator_build_language_from_text(&loaded, lang, json_text, use_catalog)) {
        return 0;
    }

    return sw_translator_store_language(translator, &loaded);
}

static b8 sw_translator_load_file(sw_translator* translator, const c8* lang, const c8* path, b8 use_catalog) {
    c8* json_text;
    b8 ok;

    if (translator == NULL || lang == NULL || path == NULL) {
        return 0;
    }

    json_text = sw_get_file_content(path, NULL);
    if (json_text == NULL) {
        return 0;
    }

    ok = sw_translator_load_text(translator, lang, json_text, use_catalog);
    free(json_text);
    return ok;
}

static b8 sw_translator_collect_catalog_languages(sw_translation_language_array* languages, const s_json* root) {
    sz i;
    sw_translation_language english;

    if (languages == NULL || root == NULL || root->type != S_JSON_OBJECT) {
        return 0;
    }

    s_array_init(languages);

    if (!sw_translator_build_empty_language(&english, "en")) {
        return 0;
    }
    s_array_add(languages, english);

    for (i = 0; i < root->as.children.count; ++i) {
        const s_json* child = root->as.children.items[i];
        sz j;

        if (child == NULL
            || child->name == NULL
            || child->type != S_JSON_OBJECT
            || sw_json_object_name_seen_before(root, i, child->name)) {
            sw_translation_languages_free(languages);
            return 0;
        }

        for (j = 0; j < child->as.children.count; ++j) {
            const s_json* locale = child->as.children.items[j];
            sw_translation_language built;

            if (locale == NULL
                || locale->name == NULL
                || locale->type != S_JSON_STRING
                || locale->as.string == NULL
                || sw_json_object_name_seen_before(child, j, locale->name)) {
                sw_translation_languages_free(languages);
                return 0;
            }

            if (sw_translation_languages_has_code(languages, locale->name)) {
                continue;
            }

            if (!sw_translator_build_catalog_language(&built, locale->name, root)) {
                sw_translation_languages_free(languages);
                return 0;
            }

            s_array_add(languages, built);
        }
    }

    return languages->b.size > 0;
}

static b8 sw_translator_load_catalog_all_text(sw_translator* translator, const c8* json_text) {
    s_json_error error = {0};
    s_json* root;
    sw_translation_language_array languages;
    sz i;

    if (translator == NULL || json_text == NULL) {
        return 0;
    }

    root = s_json_parse_with_error(json_text, &error);
    if (root == NULL) {
        return 0;
    }

    if (!sw_translator_collect_catalog_languages(&languages, root)) {
        s_json_free(root);
        return 0;
    }

    s_json_free(root);

    for (i = 0; i < languages.b.size; ++i) {
        sw_translation_language* language = &((sw_translation_language*)languages.b.data)[i];

        if (!sw_translator_store_language(translator, language)) {
            sw_translation_languages_free(&languages);
            return 0;
        }
    }

    sw_translation_languages_free(&languages);
    return 1;
}

static b8 sw_translator_load_catalog_all_file(sw_translator* translator, const c8* path) {
    c8* json_text;
    b8 ok;

    if (translator == NULL || path == NULL) {
        return 0;
    }

    json_text = sw_get_file_content(path, NULL);
    if (json_text == NULL) {
        return 0;
    }

    ok = sw_translator_load_catalog_all_text(translator, json_text);
    free(json_text);
    return ok;
}

sw_translator* sw_translator_create(void) {
    sw_translator* translator = (sw_translator*)calloc(1, sizeof(*translator));
    if (translator != NULL) {
        s_array_init(&translator->languages);
        translator->current_language = S_HANDLE_NULL;
    }
    return translator;
}

void sw_translator_destroy(sw_translator* translator) {
    sz i;
    sw_translation_language* data;

    if (translator == NULL) {
        return;
    }

    data = (sw_translation_language*)translator->languages.b.data;
    for (i = 0; i < translator->languages.b.size; ++i) {
        sw_translation_language_dispose(&data[i]);
    }

    s_array_clear(&translator->languages);
    free(translator);
}

b8 sw_translator_load_json_text(sw_translator* translator, const c8* lang, const c8* json_text) {
    return sw_translator_load_text(translator, lang, json_text, 0);
}

b8 sw_translator_load_json_file(sw_translator* translator, const c8* lang, const c8* path) {
    return sw_translator_load_file(translator, lang, path, 0);
}

b8 sw_translator_load_catalog_json_text(sw_translator* translator, const c8* lang, const c8* json_text) {
    return sw_translator_load_text(translator, lang, json_text, 1);
}

b8 sw_translator_load_catalog_json_file(sw_translator* translator, const c8* lang, const c8* path) {
    return sw_translator_load_file(translator, lang, path, 1);
}

b8 sw_translator_load_catalog_all_json_text(sw_translator* translator, const c8* json_text) {
    return sw_translator_load_catalog_all_text(translator, json_text);
}

b8 sw_translator_load_catalog_all_json_file(sw_translator* translator, const c8* path) {
    return sw_translator_load_catalog_all_file(translator, path);
}

b8 sw_translator_set_language(sw_translator* translator, const c8* lang) {
    s_handle handle = S_HANDLE_NULL;

    if (sw_translator_find_language(translator, lang, &handle) == NULL) {
        return 0;
    }

    translator->current_language = handle;
    return 1;
}

const c8* sw_translator_get_language(const sw_translator* translator) {
    const sw_translation_language* language = sw_translator_current_language(translator);

    if (language == NULL || language->code == NULL) {
        return "";
    }
    return language->code;
}

const c8* sw_translate(const sw_translator* translator, const c8* str) {
    const sw_translation_language* language;
    sz i;
    sw_translation_item* items;

    if (str == NULL) {
        return NULL;
    }

    language = sw_translator_current_language(translator);
    if (language == NULL) {
        return str;
    }

    items = (sw_translation_item*)language->items.b.data;
    for (i = 0; i < language->items.b.size; ++i) {
        if (strcmp(str, items[i].key) == 0) {
            return items[i].value;
        }
    }

    return str;
}
