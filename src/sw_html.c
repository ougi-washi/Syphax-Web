#include "sw_internal.h"

static b8 sw_html_append_escaped(sw_html_buffer* buffer, const c8* text, b8 attribute) {
    sz i;

    if (text == NULL) {
        return 1;
    }

    for (i = 0; text[i] != '\0'; ++i) {
        switch (text[i]) {
            case '&':
                if (!sw_char_array_append_cstr(&buffer->bytes, "&amp;")) return 0;
                break;
            case '<':
                if (!sw_char_array_append_cstr(&buffer->bytes, "&lt;")) return 0;
                break;
            case '>':
                if (!sw_char_array_append_cstr(&buffer->bytes, "&gt;")) return 0;
                break;
            case '"':
                if (attribute) {
                    if (!sw_char_array_append_cstr(&buffer->bytes, "&quot;")) return 0;
                } else if (!sw_char_array_append_byte(&buffer->bytes, text[i])) {
                    return 0;
                }
                break;
            case '\'':
                if (attribute) {
                    if (!sw_char_array_append_cstr(&buffer->bytes, "&#39;")) return 0;
                } else if (!sw_char_array_append_byte(&buffer->bytes, text[i])) {
                    return 0;
                }
                break;
            default:
                if (!sw_char_array_append_byte(&buffer->bytes, text[i])) return 0;
                break;
        }
    }

    return 1;
}

static b8 sw_html_append_attr_item(sw_html_buffer* buffer, const sw_html_attr_item* attr) {
    const c8* value;

    if (attr == NULL || attr->name == NULL || attr->name[0] == '\0' || !attr->enabled) {
        return 1;
    }

    if (attr->is_boolean) {
        if (!sw_char_array_append_byte(&buffer->bytes, ' ')) return 0;
        return sw_char_array_append_cstr(&buffer->bytes, attr->name);
    }

    value = attr->value;
    if (value == NULL || value[0] == '\0') {
        return 1;
    }

    if (attr->translate) {
        value = sw_translate(buffer->translator, value);
    }

    if (!sw_char_array_append_byte(&buffer->bytes, ' ')) return 0;
    if (!sw_char_array_append_cstr(&buffer->bytes, attr->name)) return 0;
    if (!sw_char_array_append_cstr(&buffer->bytes, "=\"")) return 0;
    if (!sw_html_append_escaped(buffer, value, 1)) return 0;
    return sw_char_array_append_byte(&buffer->bytes, '"');
}

static b8 sw_html_append_attr_items(sw_html_buffer* buffer, const sw_html_attr_item* attrs, sz attr_count) {
    sz i;

    if (attrs == NULL || attr_count == 0) {
        return 1;
    }

    for (i = 0; i < attr_count; ++i) {
        if (!sw_html_append_attr_item(buffer, &attrs[i])) {
            return 0;
        }
    }

    return 1;
}

sw_html_buffer* sw_html_buffer_create(void) {
    sw_html_buffer* buffer = (sw_html_buffer*)calloc(1, sizeof(*buffer));
    if (buffer == NULL) {
        return NULL;
    }
    sw_char_array_init(&buffer->bytes);
    return buffer;
}

void sw_html_buffer_destroy(sw_html_buffer* buffer) {
    if (buffer == NULL) {
        return;
    }
    sw_char_array_free(&buffer->bytes);
    free(buffer);
}

void sw_html_buffer_clear(sw_html_buffer* buffer) {
    if (buffer == NULL) {
        return;
    }
    sw_char_array_reset(&buffer->bytes);
}

void sw_html_buffer_set_translator(sw_html_buffer* buffer, const sw_translator* translator) {
    if (buffer == NULL) {
        return;
    }
    buffer->translator = translator;
}

const sw_translator* sw_html_buffer_get_translator(const sw_html_buffer* buffer) {
    if (buffer == NULL) {
        return NULL;
    }
    return buffer->translator;
}

const c8* sw_html_buffer_data(const sw_html_buffer* buffer) {
    if (buffer == NULL) {
        return "";
    }
    return sw_char_array_data(&buffer->bytes);
}

sz sw_html_buffer_size(const sw_html_buffer* buffer) {
    if (buffer == NULL) {
        return 0;
    }
    return sw_char_array_size(&buffer->bytes);
}

b8 sw_html_open_tag(sw_html_buffer* buffer, const c8* tag, const sw_html_attr_item* attrs, sz attr_count) {
    if (buffer == NULL || tag == NULL) {
        return 0;
    }
    if (!sw_char_array_append_byte(&buffer->bytes, '<')) return 0;
    if (!sw_char_array_append_cstr(&buffer->bytes, tag)) return 0;
    if (!sw_html_append_attr_items(buffer, attrs, attr_count)) return 0;
    return sw_char_array_append_byte(&buffer->bytes, '>');
}

b8 sw_html_close_tag(sw_html_buffer* buffer, const c8* tag) {
    if (buffer == NULL || tag == NULL) {
        return 0;
    }
    if (!sw_char_array_append_cstr(&buffer->bytes, "</")) return 0;
    if (!sw_char_array_append_cstr(&buffer->bytes, tag)) return 0;
    return sw_char_array_append_byte(&buffer->bytes, '>');
}

b8 sw_html_void_tag(sw_html_buffer* buffer, const c8* tag, const sw_html_attr_item* attrs, sz attr_count) {
    return sw_html_open_tag(buffer, tag, attrs, attr_count);
}

b8 sw_html_text(sw_html_buffer* buffer, const c8* text) {
    if (buffer == NULL) {
        return 0;
    }
    return sw_html_append_escaped(buffer, text, 0);
}

b8 sw_html_text_tr(sw_html_buffer* buffer, const c8* text) {
    if (buffer == NULL) {
        return 0;
    }
    return sw_html_append_escaped(buffer, sw_translate(buffer->translator, text), 0);
}

b8 sw_html_raw(sw_html_buffer* buffer, const c8* text) {
    if (buffer == NULL) {
        return 0;
    }
    return sw_char_array_append_cstr(&buffer->bytes, text);
}

b8 sw_html_rawf(sw_html_buffer* buffer, const c8* fmt, ...) {
    va_list ap;
    b8 ok;

    if (buffer == NULL || fmt == NULL) {
        return 0;
    }

    va_start(ap, fmt);
    ok = sw_char_array_append_vformat(&buffer->bytes, fmt, ap);
    va_end(ap);
    return ok;
}

b8 sw_html_title(sw_html_buffer* buffer, const c8* text) {
    if (!sw_html_open_tag(buffer, "title", NULL, 0)) return 0;
    if (!sw_html_text(buffer, text)) return 0;
    return sw_html_close_tag(buffer, "title");
}

b8 sw_html_title_tr(sw_html_buffer* buffer, const c8* text) {
    if (!sw_html_open_tag(buffer, "title", NULL, 0)) return 0;
    if (!sw_html_text_tr(buffer, text)) return 0;
    return sw_html_close_tag(buffer, "title");
}

b8 sw_html_meta_charset(sw_html_buffer* buffer, const c8* charset) {
    return sw_html_void_tag(buffer, "meta", sw_html_attr_items(sw_html_attr_kv("charset", charset)));
}
