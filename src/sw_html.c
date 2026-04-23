#include "sw_html.h"
#include "sw_internal.h"

#include <ctype.h>
#include <stdlib.h>

static const c8 sw_html_translation_attr_name[] = "__sw_translation__";
static const c8 sw_html_direction_attr_name[] = "__sw_direction__";
static const c8 sw_html_direction_data_attr_name[] = "data-sw-direction";

static b8 sw_html_is_root_html(const c8* tag) {
    static const c8 html_tag[] = "html";
    sz i;

    if (tag == NULL) {
        return 0;
    }

    for (i = 0; html_tag[i] != '\0'; ++i) {
        if (tag[i] == '\0') {
            return 0;
        }
        if (tolower((unsigned char)tag[i]) != html_tag[i]) {
            return 0;
        }
    }

    return tag[i] == '\0';
}

static b8 sw_html_maybe_emit_doctype(sw_buffer* h, const c8* tag) {
    if (h == NULL || h->html_doctype_emitted || sw_char_array_size(&h->bytes) != 0) {
        return 1;
    }

    if (!sw_html_is_root_html(tag)) {
        return 1;
    }

    if (!sw_char_array_append_cstr(&h->bytes, "<!doctype html>")) {
        return 0;
    }

    h->html_doctype_emitted = 1;
    return 1;
}

static b8 sw_html_translation_active(const sw_buffer* h) {
    return h != NULL && h->translation_enabled && h->translator != NULL;
}

static const sw_language* sw_html_current_language(const sw_buffer* h) {
    sw_languages* languages;

    if (h == NULL || h->translator == NULL || h->translator->current_language == S_HANDLE_NULL) {
        return NULL;
    }

    languages = (sw_languages*)&h->translator->languages;
    return s_array_get(languages, h->translator->current_language);
}

static const c8* sw_html_direction_dir_name(sw_language_direction direction) {
    return direction == SW_LANGUAGE_DIRECTION_RTL ? "rtl" : "ltr";
}

static const c8* sw_html_direction_style_name(sw_language_direction direction) {
    return direction == SW_LANGUAGE_DIRECTION_TTB ? "writing-mode:vertical-rl;text-orientation:mixed;" : NULL;
}

static const c8* sw_html_translate_if_needed(const sw_buffer* h, const c8* text, b8 no_translate) {
    if (text == NULL || no_translate || !sw_html_translation_active(h)) {
        return text;
    }

    return sw_translate(h->translator, text);
}

static b8 sw_html_is_translation_attr(const c8* name) {
    return name != NULL && sw_stricmp_ascii(name, sw_html_translation_attr_name) == 0;
}

static b8 sw_html_is_direction_attr(const c8* name) {
    return name != NULL && sw_stricmp_ascii(name, sw_html_direction_attr_name) == 0;
}

static b8 sw_html_is_internal_attr(const c8* name) {
    return sw_html_is_translation_attr(name) || sw_html_is_direction_attr(name);
}

static b8 sw_html_parse_bool(const c8* value, b8 fallback) {
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    if (value[0] == '0'
        || value[0] == 'f'
        || value[0] == 'F'
        || value[0] == 'n'
        || value[0] == 'N') {
        return 0;
    }

    return 1;
}

static sw_language_direction sw_html_parse_direction(const c8* value, sw_language_direction fallback) {
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }

    if (sw_stricmp_ascii(value, "rtl") == 0) {
        return SW_LANGUAGE_DIRECTION_RTL;
    }
    if (sw_stricmp_ascii(value, "ttb") == 0
        || sw_stricmp_ascii(value, "vertical") == 0
        || sw_stricmp_ascii(value, "vertical-rl") == 0) {
        return SW_LANGUAGE_DIRECTION_TTB;
    }

    return SW_LANGUAGE_DIRECTION_LTR;
}

static b8 sw_html_resolve_translation_enabled(const sw_buffer* h, const sw_attr_item* attrs, sz attr_count) {
    b8 enabled = h != NULL ? h->translation_enabled : 0;
    sz i;

    for (i = 0; i < attr_count; ++i) {
        if (!attrs[i].enabled || !sw_html_is_translation_attr(attrs[i].name)) {
            continue;
        }

        enabled = sw_html_parse_bool(attrs[i].value, enabled);
    }

    return enabled;
}

static b8 sw_html_resolve_direction(
    const sw_buffer* h,
    const c8* tag,
    const sw_attr_item* attrs,
    sz attr_count,
    sw_language_direction* out_direction
) {
    const sw_language* language = NULL;
    sw_language_direction direction = SW_LANGUAGE_DIRECTION_LTR;
    b8 has_direction = 0;
    sz i;

    if (sw_html_is_root_html(tag) && sw_html_translation_active(h)) {
        language = sw_html_current_language(h);
        if (language != NULL) {
            direction = language->direction;
            has_direction = 1;
        }
    }

    for (i = 0; i < attr_count; ++i) {
        if (!attrs[i].enabled || !sw_html_is_direction_attr(attrs[i].name)) {
            continue;
        }

        direction = sw_html_parse_direction(attrs[i].value, direction);
        has_direction = 1;
    }

    if (out_direction != NULL) {
        *out_direction = direction;
    }

    return has_direction;
}

static b8 sw_html_has_enabled_attr(const sw_attr_item* attrs, sz attr_count, const c8* name) {
    sz i;

    if (attrs == NULL || name == NULL) {
        return 0;
    }

    for (i = 0; i < attr_count; ++i) {
        if (attrs[i].enabled && attrs[i].name != NULL && sw_stricmp_ascii(attrs[i].name, name) == 0) {
            return 1;
        }
    }

    return 0;
}

static b8 sw_html_has_internal_direction_attr(const sw_attr_item* attrs, sz attr_count) {
    sz i;

    if (attrs == NULL) {
        return 0;
    }

    for (i = 0; i < attr_count; ++i) {
        if (attrs[i].enabled && sw_html_is_direction_attr(attrs[i].name)) {
            return 1;
        }
    }

    return 0;
}

static b8 sw_html_attr_allows_translation(const c8* name) {
    if (name == NULL) {
        return 0;
    }

    return sw_stricmp_ascii(name, "title") == 0
        || sw_stricmp_ascii(name, "placeholder") == 0
        || sw_stricmp_ascii(name, "alt") == 0
        || sw_stricmp_ascii(name, "aria-label") == 0
        || sw_stricmp_ascii(name, "aria-description") == 0
        || sw_stricmp_ascii(name, "aria-placeholder") == 0
        || sw_stricmp_ascii(name, "aria-valuetext") == 0;
}

static b8 sw_html_append_escaped(sw_buffer* h, const c8* text, b8 attribute) {
    sz i;

    if (text == NULL) {
        return 1;
    }

    for (i = 0; text[i] != '\0'; ++i) {
        switch (text[i]) {
            case '&':
                if (!sw_char_array_append_cstr(&h->bytes, "&amp;")) return 0;
                break;
            case '<':
                if (!sw_char_array_append_cstr(&h->bytes, "&lt;")) return 0;
                break;
            case '>':
                if (!sw_char_array_append_cstr(&h->bytes, "&gt;")) return 0;
                break;
            case '"':
                if (attribute) {
                    if (!sw_char_array_append_cstr(&h->bytes, "&quot;")) return 0;
                } else if (!sw_char_array_append_byte(&h->bytes, text[i])) {
                    return 0;
                }
                break;
            case '\'':
                if (attribute) {
                    if (!sw_char_array_append_cstr(&h->bytes, "&#39;")) return 0;
                } else if (!sw_char_array_append_byte(&h->bytes, text[i])) {
                    return 0;
                }
                break;
            default:
                if (!sw_char_array_append_byte(&h->bytes, text[i])) return 0;
                break;
        }
    }

    return 1;
}

static b8 sw_html_append_attr(sw_buffer* h, const sw_attr_item* attr) {
    const c8* value;

    if (attr == NULL || attr->name == NULL || attr->name[0] == '\0' || !attr->enabled || sw_html_is_internal_attr(attr->name)) {
        return 1;
    }

    if (attr->is_boolean) {
        if (!sw_char_array_append_byte(&h->bytes, ' ')) return 0;
        return sw_char_array_append_cstr(&h->bytes, attr->name);
    }

    value = attr->value;
    if (value == NULL || value[0] == '\0') {
        return 1;
    }

    if (!attr->no_translate && sw_html_attr_allows_translation(attr->name)) {
        value = sw_html_translate_if_needed(h, value, 0);
    }

    if (!sw_char_array_append_byte(&h->bytes, ' ')) return 0;
    if (!sw_char_array_append_cstr(&h->bytes, attr->name)) return 0;
    if (!sw_char_array_append_cstr(&h->bytes, "=\"")) return 0;
    if (!sw_html_append_escaped(h, value, 1)) return 0;
    return sw_char_array_append_byte(&h->bytes, '"');
}

static b8 sw_html_style_needs_separator(const c8* value) {
    sz i;

    if (value == NULL) {
        return 0;
    }

    for (i = 0; value[i] != '\0'; ++i) {
    }

    while (i > 0) {
        i -= 1;
        if (!isspace((unsigned char)value[i])) {
            return value[i] != ';';
        }
    }

    return 0;
}

static b8 sw_html_append_style_attr(sw_buffer* h, const sw_attr_item* attr, const c8* extra_style) {
    const c8* value = attr != NULL ? attr->value : NULL;

    if (h == NULL || attr == NULL || attr->name == NULL || attr->name[0] == '\0' || !attr->enabled) {
        return 0;
    }
    if ((value == NULL || value[0] == '\0') && (extra_style == NULL || extra_style[0] == '\0')) {
        return 1;
    }

    if (!sw_char_array_append_byte(&h->bytes, ' ')) return 0;
    if (!sw_char_array_append_cstr(&h->bytes, attr->name)) return 0;
    if (!sw_char_array_append_cstr(&h->bytes, "=\"")) return 0;
    if (value != NULL && value[0] != '\0') {
        if (!sw_html_append_escaped(h, value, 1)) return 0;
        if (extra_style != NULL && extra_style[0] != '\0' && sw_html_style_needs_separator(value)) {
            if (!sw_char_array_append_byte(&h->bytes, ';')) return 0;
        }
    }
    if (extra_style != NULL && extra_style[0] != '\0') {
        if (!sw_html_append_escaped(h, extra_style, 1)) return 0;
    }
    return sw_char_array_append_byte(&h->bytes, '"');
}

static b8 sw_html_append_attrs(
    sw_buffer* h,
    const sw_attr_item* attrs,
    sz attr_count,
    b8 has_direction,
    sw_language_direction direction
) {
    sz i;
    const b8 has_explicit_dir = sw_html_has_enabled_attr(attrs, attr_count, "dir");
    const b8 has_internal_direction = sw_html_has_internal_direction_attr(attrs, attr_count);
    const c8* extra_style = has_direction ? sw_html_direction_style_name(direction) : NULL;
    b8 wrote_style = 0;

    if (has_direction && !has_explicit_dir) {
        const sw_attr_item attr = sw_attr("dir", sw_html_direction_dir_name(direction));

        if (!sw_html_append_attr(h, &attr)) {
            return 0;
        }
    }

    if (has_internal_direction && !sw_html_has_enabled_attr(attrs, attr_count, sw_html_direction_data_attr_name)) {
        const sw_attr_item attr = sw_attr(sw_html_direction_data_attr_name, sw_direction_value(direction));

        if (!sw_html_append_attr(h, &attr)) {
            return 0;
        }
    }

    if (attrs == NULL || attr_count == 0) {
        if (extra_style != NULL) {
            const sw_attr_item style_attr = sw_attr("style", extra_style);

            return sw_html_append_attr(h, &style_attr);
        }
        return 1;
    }

    for (i = 0; i < attr_count; ++i) {
        if (extra_style != NULL
            && attrs[i].enabled
            && attrs[i].name != NULL
            && sw_stricmp_ascii(attrs[i].name, "style") == 0) {
            if (!sw_html_append_style_attr(h, &attrs[i], extra_style)) {
                return 0;
            }
            wrote_style = 1;
            continue;
        }

        if (!sw_html_append_attr(h, &attrs[i])) {
            return 0;
        }
    }

    if (extra_style != NULL && !wrote_style) {
        const sw_attr_item style_attr = sw_attr("style", extra_style);

        if (!sw_html_append_attr(h, &style_attr)) {
            return 0;
        }
    }

    return 1;
}

static void sw_html_push_translation_scope(sw_buffer* h, b8 previous_translation_enabled) {
    if (h == NULL) {
        return;
    }

    s_array_add(&h->translation_stack, previous_translation_enabled);
}

static void sw_html_pop_translation_scope(sw_buffer* h) {
    const sz size = h != NULL ? s_array_get_size(&h->translation_stack) : 0;

    if (h == NULL || size == 0) {
        return;
    }

    {
        const s_handle handle = s_array_handle(&h->translation_stack, (u32)(size - 1));
        const b8* previous = s_array_get(&h->translation_stack, handle);
        h->translation_enabled = previous != NULL && *previous ? 1 : 0;
        s_array_remove(&h->translation_stack, handle);
    }
}

static b8 sw_html_append_auto_root_attrs(sw_buffer* h, const c8* tag, const sw_attr_item* attrs, sz attr_count) {
    const sw_language* language;
    sw_attr_item attr;

    if (!sw_html_is_root_html(tag) || !sw_html_translation_active(h)) {
        return 1;
    }

    language = sw_html_current_language(h);
    if (language == NULL) {
        return 1;
    }

    if (!sw_html_has_enabled_attr(attrs, attr_count, "lang")) {
        attr = sw_attr("lang", language->code);
        if (!sw_html_append_attr(h, &attr)) {
            return 0;
        }
    }

    return 1;
}

static b8 sw_html_open_tag(sw_buffer* h, const c8* tag, sw_attr_list attrs, b8 push_scope) {
    const b8 previous_translation_enabled = h != NULL ? h->translation_enabled : 0;
    const b8 next_translation_enabled = sw_html_resolve_translation_enabled(h, attrs.items, attrs.count);
    sw_language_direction direction = SW_LANGUAGE_DIRECTION_LTR;
    const b8 has_direction = sw_html_resolve_direction(h, tag, attrs.items, attrs.count, &direction);
    b8 ok = 0;

    if (h == NULL || tag == NULL) {
        return 0;
    }

    h->translation_enabled = next_translation_enabled;

    do {
        if (!sw_html_maybe_emit_doctype(h, tag)) break;
        if (!sw_char_array_append_byte(&h->bytes, '<')) break;
        if (!sw_char_array_append_cstr(&h->bytes, tag)) break;
        if (!sw_html_append_auto_root_attrs(h, tag, attrs.items, attrs.count)) break;
        if (!sw_html_append_attrs(h, attrs.items, attrs.count, has_direction, direction)) break;
        if (!sw_char_array_append_byte(&h->bytes, '>')) break;
        ok = 1;
    } while (0);

    if (!ok) {
        h->translation_enabled = previous_translation_enabled;
        return 0;
    }

    if (push_scope) {
        sw_html_push_translation_scope(h, previous_translation_enabled);
    } else {
        h->translation_enabled = previous_translation_enabled;
    }

    return 1;
}

sw_buffer* sw_buffer_new(void) {
    sw_buffer* h = (sw_buffer*)calloc(1, sizeof(*h));

    if (h == NULL) {
        return NULL;
    }

    sw_char_array_init(&h->bytes);
    s_array_init(&h->translation_stack);
    h->translation_enabled = 1;
    return h;
}

void sw_buffer_free(sw_buffer* h) {
    if (h == NULL) {
        return;
    }

    sw_char_array_free(&h->bytes);
    s_array_clear(&h->translation_stack);
    free(h);
}

void sw_buffer_reset(sw_buffer* h) {
    if (h == NULL) {
        return;
    }

    sw_char_array_reset(&h->bytes);
    s_array_clear(&h->translation_stack);
    s_array_init(&h->translation_stack);
    h->translation_enabled = 1;
    h->html_doctype_emitted = 0;
    h->js_runtime_emitted = 0;
}

void sw_buffer_set_translator(sw_buffer* h, const sw_translator* translator) {
    if (h == NULL) {
        return;
    }

    h->translator = translator;
}

const sw_translator* sw_buffer_get_translator(const sw_buffer* h) {
    if (h == NULL) {
        return NULL;
    }

    return h->translator;
}

void sw_buffer_set_translation(sw_buffer* h, b8 enabled) {
    if (h == NULL) {
        return;
    }

    h->translation_enabled = enabled ? 1 : 0;
}

b8 sw_buffer_translation_enabled(const sw_buffer* h) {
    if (h == NULL) {
        return 0;
    }

    return h->translation_enabled;
}

const c8* sw_buffer_data(const sw_buffer* h) {
    if (h == NULL) {
        return "";
    }

    return sw_char_array_data(&h->bytes);
}

sz sw_buffer_len(const sw_buffer* h) {
    if (h == NULL) {
        return 0;
    }

    return sw_char_array_size(&h->bytes);
}

b8 sw_tag(sw_buffer* h, const c8* tag, sw_attr_list attrs) {
    return sw_html_open_tag(h, tag, attrs, 1);
}

b8 sw_end(sw_buffer* h, const c8* tag) {
    b8 ok;

    if (h == NULL || tag == NULL) {
        return 0;
    }

    ok = sw_char_array_append_cstr(&h->bytes, "</")
        && sw_char_array_append_cstr(&h->bytes, tag)
        && sw_char_array_append_byte(&h->bytes, '>');

    sw_html_pop_translation_scope(h);
    return ok;
}

b8 sw_void(sw_buffer* h, const c8* tag, sw_attr_list attrs) {
    return sw_html_open_tag(h, tag, attrs, 0);
}

b8 sw_text(sw_buffer* h, const c8* text) {
    if (h == NULL) {
        return 0;
    }

    return sw_html_append_escaped(h, sw_html_translate_if_needed(h, text, 0), 0);
}

b8 sw_text_no_translate(sw_buffer* h, const c8* text) {
    if (h == NULL) {
        return 0;
    }

    return sw_html_append_escaped(h, text, 0);
}

b8 sw_raw(sw_buffer* h, const c8* text) {
    if (h == NULL) {
        return 0;
    }

    return sw_char_array_append_cstr(&h->bytes, text);
}

b8 sw_rawf(sw_buffer* h, const c8* fmt, ...) {
    va_list ap;
    b8 ok;

    if (h == NULL || fmt == NULL) {
        return 0;
    }

    va_start(ap, fmt);
    ok = sw_char_array_append_vformat(&h->bytes, fmt, ap);
    va_end(ap);
    return ok;
}

b8 sw_title(sw_buffer* h, const c8* text) {
    if (!sw_tag(h, "title", sw_no_attrs)) return 0;
    if (!sw_text(h, text)) return 0;
    return sw_end(h, "title");
}

b8 sw_title_no_translate(sw_buffer* h, const c8* text) {
    if (!sw_tag(h, "title", sw_no_attrs)) return 0;
    if (!sw_text_no_translate(h, text)) return 0;
    return sw_end(h, "title");
}

b8 sw_meta_charset(sw_buffer* h, const c8* charset) {
    return sw_void(h, "meta", sw_attrs(sw_attr("charset", charset)));
}
