#include "sw_translator.h"
#include "sw_utility.h"
#include "sw_internal.h"
#include "syphax/s_json.h"

#include <stdlib.h>
#include <string.h>

static void sw_language_dispose(sw_language* language);
static void sw_translation_language_dispose(sw_translation_language* language);

static void sw_language_dispose(sw_language* language) {
    if (language == NULL) {
        return;
    }

    free((void*)language->code);
    free((void*)language->label);
    memset(language, 0, sizeof(*language));
}

static b8 sw_registered_language_begin(sw_language* language, const sw_language* source) {
    const c8* label;

    if (language == NULL || source == NULL || source->code == NULL || source->code[0] == '\0') {
        return 0;
    }

    label = (source->label != NULL) ? source->label : source->code;

    memset(language, 0, sizeof(*language));

    language->code = sw_strdup_cstr(source->code);
    language->label = sw_strdup_cstr(label);
    language->direction = source->direction;
    if (language->code == NULL || language->label == NULL) {
        sw_language_dispose(language);
        return 0;
    }

    return 1;
}

static void sw_translation_items_free(sw_translation_item_array* items) {
    sz i;
    sw_translation_item* data;

    if (items == NULL) {
        return;
    }

    data = s_array_get_data(items);
    for (i = 0; i < s_array_get_size(items); ++i) {
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

    data = s_array_get_data(languages);
    for (i = 0; i < s_array_get_size(languages); ++i) {
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
    memset(language, 0, sizeof(*language));
}

static sw_language* sw_translator_find_registered_language(
    sw_translator* translator,
    const c8* code,
    s_handle* out_handle
) {
    sz i;
    sw_language* data;

    if (out_handle != NULL) {
        *out_handle = S_HANDLE_NULL;
    }
    if (translator == NULL || code == NULL) {
        return NULL;
    }

    data = s_array_get_data(&translator->languages);
    for (i = 0; i < s_array_get_size(&translator->languages); ++i) {
        if (data[i].code != NULL && strcmp(data[i].code, code) == 0) {
            if (out_handle != NULL) {
                *out_handle = s_array_handle(&translator->languages, (u32)i);
            }
            return &data[i];
        }
    }

    return NULL;
}

static sw_translation_language* sw_translator_find_translation_language(
    sw_translator* translator,
    const c8* code,
    s_handle* out_handle
) {
    sz i;
    sw_translation_language* data;

    if (out_handle != NULL) {
        *out_handle = S_HANDLE_NULL;
    }
    if (translator == NULL || code == NULL) {
        return NULL;
    }

    data = s_array_get_data(&translator->translations);
    for (i = 0; i < s_array_get_size(&translator->translations); ++i) {
        if (data[i].code != NULL && strcmp(data[i].code, code) == 0) {
            if (out_handle != NULL) {
                *out_handle = s_array_handle(&translator->translations, (u32)i);
            }
            return &data[i];
        }
    }

    return NULL;
}


static const sw_translation_language* sw_translator_current_translation_language(const sw_translator* translator) {
    const sw_language* language = sw_translator_get_language(translator);

    if (language == NULL || language->code == NULL) {
        return NULL;
    }

    return sw_translator_find_translation_language((sw_translator*)translator, language->code, NULL);
}

static b8 sw_translation_items_has_key(const sw_translation_item_array* items, const c8* key) {
    sz i;
    sw_translation_item* data;

    if (items == NULL || key == NULL) {
        return 0;
    }

    data = s_array_get_data((sw_translation_item_array*)items);
    for (i = 0; i < s_array_get_size(items); ++i) {
        if (strcmp(data[i].key, key) == 0) {
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

static b8 sw_translation_language_begin(sw_translation_language* language, const c8* code) {
    if (language == NULL || code == NULL || code[0] == '\0') {
        return 0;
    }

    memset(language, 0, sizeof(*language));
    s_array_init(&language->items);

    language->code = sw_strdup_cstr(code);
    if (language->code == NULL) {
        sw_translation_items_free(&language->items);
        return 0;
    }

    return 1;
}

static b8 sw_translator_build_flat_language(sw_translation_language* language, const c8* code, const s_json* root) {
    sz i;

    if (language == NULL || code == NULL || root == NULL || root->type != S_JSON_OBJECT) {
        return 0;
    }

    if (!sw_translation_language_begin(language, code)) {
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

static b8 sw_translator_build_language_from_text(sw_translation_language* language, const c8* code, const c8* json_text) {
    s_json_error error = {0};
    s_json* root;
    b8 ok;

    if (language == NULL || code == NULL || json_text == NULL) {
        return 0;
    }

    root = s_json_parse_with_error(json_text, &error);
    if (root == NULL) {
        return 0;
    }

    ok = sw_translator_build_flat_language(language, code, root);

    s_json_free(root);
    return ok;
}

static b8 sw_translator_upsert_registered_language(
    sw_translator* translator,
    const sw_language* language,
    s_handle* out_handle
) {
    sw_language owned;
    sw_language* existing;
    s_handle handle = S_HANDLE_NULL;

    if (out_handle != NULL) {
        *out_handle = S_HANDLE_NULL;
    }
    if (translator == NULL || language == NULL || language->code == NULL || language->code[0] == '\0') {
        return 0;
    }

    if (!sw_registered_language_begin(&owned, language)) {
        return 0;
    }

    existing = sw_translator_find_registered_language(translator, language->code, &handle);
    if (existing != NULL) {
        sw_language_dispose(existing);
        *existing = owned;
        if (out_handle != NULL) {
            *out_handle = handle;
        }
        return 1;
    }

    handle = s_array_add(&translator->languages, owned);
    if (translator->current_language == S_HANDLE_NULL) {
        translator->current_language = handle;
    }
    if (out_handle != NULL) {
        *out_handle = handle;
    }
    return 1;
}

static b8 sw_translator_ensure_registered_language(sw_translator* translator, const c8* code) {
    sw_language fallback = {0};

    if (translator == NULL || code == NULL || code[0] == '\0') {
        return 0;
    }
    if (sw_translator_find_registered_language(translator, code, NULL) != NULL) {
        return 1;
    }

    fallback.code = code;
    return sw_translator_upsert_registered_language(translator, &fallback, NULL);
}

static b8 sw_translator_store_language(sw_translator* translator, sw_translation_language* loaded) {
    sw_translation_language* existing;
    s_handle existing_handle = S_HANDLE_NULL;

    if (translator == NULL || loaded == NULL || loaded->code == NULL) {
        return 0;
    }
    if (!sw_translator_ensure_registered_language(translator, loaded->code)) {
        return 0;
    }

    existing = sw_translator_find_translation_language(translator, loaded->code, &existing_handle);
    if (existing != NULL) {
        sw_translation_language_dispose(existing);
        *existing = *loaded;
        memset(loaded, 0, sizeof(*loaded));
        return 1;
    }

    s_array_add(&translator->translations, *loaded);
    memset(loaded, 0, sizeof(*loaded));
    return 1;
}

static b8 sw_translator_load_text(sw_translator* translator, const c8* code, const c8* json_text) {
    sw_translation_language loaded;

    if (translator == NULL || code == NULL || json_text == NULL) {
        return 0;
    }

    if (!sw_translator_build_language_from_text(&loaded, code, json_text)) {
        return 0;
    }

    return sw_translator_store_language(translator, &loaded);
}

static b8 sw_translator_load_file(sw_translator* translator, const c8* code, const c8* path) {
    c8* json_text;
    b8 ok;

    if (translator == NULL || code == NULL || path == NULL) {
        return 0;
    }

    json_text = sw_get_file_content(path, NULL);
    if (json_text == NULL) {
        return 0;
    }

    ok = sw_translator_load_text(translator, code, json_text);
    free(json_text);
    return ok;
}

static b8 sw_translator_collect_languages(sw_translation_language_array* languages, const s_json* root) {
    sz i;

    if (languages == NULL || root == NULL || root->type != S_JSON_OBJECT) {
        return 0;
    }

    s_array_init(languages);

    for (i = 0; i < root->as.children.count; ++i) {
        const s_json* child = root->as.children.items[i];
        sw_translation_language built;

        if (child == NULL
            || child->name == NULL
            || child->type != S_JSON_OBJECT
            || sw_json_object_name_seen_before(root, i, child->name)) {
            sw_translation_languages_free(languages);
            return 0;
        }

        if (!sw_translator_build_flat_language(&built, child->name, child)) {
            sw_translation_languages_free(languages);
            return 0;
        }

        s_array_add(languages, built);
    }

    return 1;
}

static b8 sw_translator_load_all_text(sw_translator* translator, const c8* json_text) {
    s_json_error error = {0};
    s_json* root;
    sw_translation_language_array languages;
    sw_translation_language* data;
    sz i;

    if (translator == NULL || json_text == NULL) {
        return 0;
    }

    root = s_json_parse_with_error(json_text, &error);
    if (root == NULL) {
        return 0;
    }

    if (!sw_translator_collect_languages(&languages, root)) {
        s_json_free(root);
        return 0;
    }

    s_json_free(root);

    data = s_array_get_data(&languages);
    for (i = 0; i < s_array_get_size(&languages); ++i) {
        sw_translation_language* language = &data[i];

        if (!sw_translator_store_language(translator, language)) {
            sw_translation_languages_free(&languages);
            return 0;
        }
    }

    sw_translation_languages_free(&languages);
    return 1;
}

static b8 sw_translator_load_all_file(sw_translator* translator, const c8* path) {
    c8* json_text;
    b8 ok;

    if (translator == NULL || path == NULL) {
        return 0;
    }

    json_text = sw_get_file_content(path, NULL);
    if (json_text == NULL) {
        return 0;
    }

    ok = sw_translator_load_all_text(translator, json_text);
    free(json_text);
    return ok;
}

sw_translator* sw_translator_create_internal(const c8* translations_path, const sw_language* default_language) {
    sw_translator* translator;
    s_handle handle = S_HANDLE_NULL;

    if (default_language == NULL || default_language->code == NULL || default_language->code[0] == '\0') {
        return NULL;
    }

    translator = (sw_translator*)calloc(1, sizeof(*translator));
    if (translator == NULL) {
        return NULL;
    }

    s_array_init(&translator->languages);
    s_array_init(&translator->translations);
    translator->current_language = S_HANDLE_NULL;

    if (!sw_translator_upsert_registered_language(translator, default_language, &handle)) {
        sw_translator_destroy(translator);
        return NULL;
    }

    translator->current_language = handle;

    if (translations_path != NULL && translations_path[0] != '\0') {
        if (!sw_translator_load_all_file(translator, translations_path)) {
            sw_translator_destroy(translator);
            return NULL;
        }
    }

    return translator;
}

void sw_add_language_internal(sw_translator* translator, const sw_language* language) {
    (void)sw_translator_upsert_registered_language(translator, language, NULL);
}

void sw_translator_destroy(sw_translator* translator) {
    sz i;
    sw_language* languages;
    sw_translation_language* translations;

    if (translator == NULL) {
        return;
    }

    languages = s_array_get_data(&translator->languages);
    for (i = 0; i < s_array_get_size(&translator->languages); ++i) {
        sw_language_dispose(&languages[i]);
    }
    s_array_clear(&translator->languages);

    translations = s_array_get_data(&translator->translations);
    for (i = 0; i < s_array_get_size(&translator->translations); ++i) {
        sw_translation_language_dispose(&translations[i]);
    }
    s_array_clear(&translator->translations);

    free(translator);
}

b8 sw_translator_load_json_text(sw_translator* translator, const c8* code, const c8* json_text) {
    return sw_translator_load_text(translator, code, json_text);
}

b8 sw_translator_load_json_file(sw_translator* translator, const c8* code, const c8* path) {
    return sw_translator_load_file(translator, code, path);
}

b8 sw_translator_load_all_json_text(sw_translator* translator, const c8* json_text) {
    return sw_translator_load_all_text(translator, json_text);
}

b8 sw_translator_load_all_json_file(sw_translator* translator, const c8* path) {
    return sw_translator_load_all_file(translator, path);
}

b8 sw_translator_set_language(sw_translator* translator, const c8* code) {
    s_handle handle = S_HANDLE_NULL;

    if (sw_translator_find_registered_language(translator, code, &handle) == NULL) {
        return 0;
    }

    translator->current_language = handle;
    return 1;
}

const sw_language* sw_translator_get_language(const sw_translator* translator) {
    sw_languages* languages;

    if (translator == NULL || translator->current_language == S_HANDLE_NULL) {
        return NULL;
    }

    languages = (sw_languages*)&translator->languages;
    return s_array_get(languages, translator->current_language);
}

const sw_language* sw_translator_get_languages(const sw_translator* translator, sz* count) {
    if (count != NULL) {
        *count = 0;
    }
    if (translator == NULL) {
        return NULL;
    }

    if (count != NULL) {
        *count = s_array_get_size(&translator->languages);
    }
    return s_array_get_data((sw_languages*)&translator->languages);
}

const c8* sw_translate(const sw_translator* translator, const c8* str) {
    const sw_translation_language* language;
    sz i;
    sw_translation_item* items;

    if (str == NULL) {
        return NULL;
    }

    language = sw_translator_current_translation_language(translator);
    if (language == NULL) {
        return str;
    }

    items = s_array_get_data((sw_translation_item_array*)&language->items);
    for (i = 0; i < s_array_get_size(&language->items); ++i) {
        if (strcmp(str, items[i].key) == 0) {
            return items[i].value;
        }
    }

    return str;
}
