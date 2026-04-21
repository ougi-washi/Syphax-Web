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

static b8 sw_html_append_attribute(sw_html_buffer* buffer, const c8* name, const c8* value, b8 translate) {
    const c8* final_value = value;

    if (value == NULL || value[0] == '\0') {
        return 1;
    }

    if (translate) {
        final_value = sw_translate(buffer->translator, value);
    }

    if (!sw_char_array_append_byte(&buffer->bytes, ' ')) return 0;
    if (!sw_char_array_append_cstr(&buffer->bytes, name)) return 0;
    if (!sw_char_array_append_cstr(&buffer->bytes, "=\"")) return 0;
    if (!sw_html_append_escaped(buffer, final_value, 1)) return 0;
    return sw_char_array_append_byte(&buffer->bytes, '"');
}

static b8 sw_html_append_boolean(sw_html_buffer* buffer, const c8* name, b8 enabled) {
    if (!enabled) {
        return 1;
    }
    if (!sw_char_array_append_byte(&buffer->bytes, ' ')) return 0;
    return sw_char_array_append_cstr(&buffer->bytes, name);
}

static b8 sw_html_append_attributes(sw_html_buffer* buffer, const sw_html_tag_attributes* attrs) {
    if (attrs == NULL) {
        return 1;
    }

    if (!sw_html_append_attribute(buffer, "id", attrs->id, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "class", attrs->class_name, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "name", attrs->name, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "rel", attrs->rel, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "placeholder", attrs->placeholder, 1)) return 0;
    if (!sw_html_append_attribute(buffer, "type", attrs->type, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "value", attrs->value, 1)) return 0;
    if (!sw_html_append_attribute(buffer, "enctype", attrs->enctype, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "for", attrs->label_for, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "method", attrs->method, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "action", attrs->action, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "rows", attrs->rows, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "cols", attrs->cols, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "href", attrs->href, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "target", attrs->target, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "src", attrs->src, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "onclick", attrs->onclick, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "width", attrs->width, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "height", attrs->height, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "frameborder", attrs->frameborder, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "charset", attrs->charset, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "content", attrs->content, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "lang", attrs->lang, 0)) return 0;
    if (!sw_html_append_attribute(buffer, "title", attrs->title, 1)) return 0;
    if (!sw_html_append_boolean(buffer, "checked", attrs->checked)) return 0;
    if (!sw_html_append_boolean(buffer, "controls", attrs->controls)) return 0;
    if (!sw_html_append_boolean(buffer, "hidden", attrs->hidden)) return 0;
    if (!sw_html_append_boolean(buffer, "defer", attrs->defer_script)) return 0;
    if (!sw_html_append_boolean(buffer, "async", attrs->async_script)) return 0;

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

b8 sw_html_open_tag(sw_html_buffer* buffer, const c8* tag, const sw_html_tag_attributes* attrs) {
    if (buffer == NULL || tag == NULL) {
        return 0;
    }
    if (!sw_char_array_append_byte(&buffer->bytes, '<')) return 0;
    if (!sw_char_array_append_cstr(&buffer->bytes, tag)) return 0;
    if (!sw_html_append_attributes(buffer, attrs)) return 0;
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

b8 sw_html_void_tag(sw_html_buffer* buffer, const c8* tag, const sw_html_tag_attributes* attrs) {
    return sw_html_open_tag(buffer, tag, attrs);
}

b8 sw_html_text(sw_html_buffer* buffer, const c8* text) {
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
    if (!sw_html_open_tag(buffer, "title", NULL)) return 0;
    if (!sw_html_text(buffer, text)) return 0;
    return sw_html_close_tag(buffer, "title");
}

b8 sw_html_meta_charset(sw_html_buffer* buffer, const c8* charset) {
    const sw_html_tag_attributes attrs = sw_html_attr(.charset = charset);
    return sw_html_void_tag(buffer, "meta", &attrs);
}
