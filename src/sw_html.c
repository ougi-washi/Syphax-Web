#include "sw_internal.h"

static b8 sw_h_is_root_html(const c8* tag) {
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

static b8 sw_h_maybe_emit_doctype(sw_hbuf* h, const c8* tag) {
    if (h == NULL || h->html_doctype_emitted || sw_char_array_size(&h->bytes) != 0) {
        return 1;
    }

    if (!sw_h_is_root_html(tag)) {
        return 1;
    }

    if (!sw_char_array_append_cstr(&h->bytes, "<!doctype html>")) {
        return 0;
    }

    h->html_doctype_emitted = 1;
    return 1;
}

static b8 sw_h_append_escaped(sw_hbuf* h, const c8* text, b8 attribute) {
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

static b8 sw_h_append_attr(sw_hbuf* h, const sw_attr* attr) {
    const c8* value;

    if (attr == NULL || attr->name == NULL || attr->name[0] == '\0' || !attr->enabled) {
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

    if (attr->translate) {
        value = sw_translate(h->translator, value);
    }

    if (!sw_char_array_append_byte(&h->bytes, ' ')) return 0;
    if (!sw_char_array_append_cstr(&h->bytes, attr->name)) return 0;
    if (!sw_char_array_append_cstr(&h->bytes, "=\"")) return 0;
    if (!sw_h_append_escaped(h, value, 1)) return 0;
    return sw_char_array_append_byte(&h->bytes, '"');
}

static b8 sw_h_append_attrs(sw_hbuf* h, const sw_attr* attrs, sz attr_count) {
    sz i;

    if (attrs == NULL || attr_count == 0) {
        return 1;
    }

    for (i = 0; i < attr_count; ++i) {
        if (!sw_h_append_attr(h, &attrs[i])) {
            return 0;
        }
    }

    return 1;
}

sw_hbuf* sw_hbuf_new(void) {
    sw_hbuf* h = (sw_hbuf*)calloc(1, sizeof(*h));

    if (h == NULL) {
        return NULL;
    }

    sw_char_array_init(&h->bytes);
    return h;
}

void sw_hbuf_free(sw_hbuf* h) {
    if (h == NULL) {
        return;
    }

    sw_char_array_free(&h->bytes);
    free(h);
}

void sw_hbuf_reset(sw_hbuf* h) {
    if (h == NULL) {
        return;
    }

    sw_char_array_reset(&h->bytes);
    h->html_doctype_emitted = 0;
    h->js_runtime_emitted = 0;
}

void sw_hbuf_set_tr(sw_hbuf* h, const sw_translator* tr) {
    if (h == NULL) {
        return;
    }

    h->translator = tr;
}

const sw_translator* sw_hbuf_get_tr(const sw_hbuf* h) {
    if (h == NULL) {
        return NULL;
    }

    return h->translator;
}

const c8* sw_hbuf_data(const sw_hbuf* h) {
    if (h == NULL) {
        return "";
    }

    return sw_char_array_data(&h->bytes);
}

sz sw_hbuf_len(const sw_hbuf* h) {
    if (h == NULL) {
        return 0;
    }

    return sw_char_array_size(&h->bytes);
}

b8 sw_tag(sw_hbuf* h, const c8* tag, sw_attr_list attrs) {
    if (h == NULL || tag == NULL) {
        return 0;
    }

    if (!sw_h_maybe_emit_doctype(h, tag)) return 0;
    if (!sw_char_array_append_byte(&h->bytes, '<')) return 0;
    if (!sw_char_array_append_cstr(&h->bytes, tag)) return 0;
    if (!sw_h_append_attrs(h, attrs.items, attrs.count)) return 0;
    return sw_char_array_append_byte(&h->bytes, '>');
}

b8 sw_end(sw_hbuf* h, const c8* tag) {
    if (h == NULL || tag == NULL) {
        return 0;
    }

    if (!sw_char_array_append_cstr(&h->bytes, "</")) return 0;
    if (!sw_char_array_append_cstr(&h->bytes, tag)) return 0;
    return sw_char_array_append_byte(&h->bytes, '>');
}

b8 sw_void(sw_hbuf* h, const c8* tag, sw_attr_list attrs) {
    return sw_tag(h, tag, attrs);
}

b8 sw_txt(sw_hbuf* h, const c8* text) {
    if (h == NULL) {
        return 0;
    }

    return sw_h_append_escaped(h, text, 0);
}

b8 sw_txt_tr(sw_hbuf* h, const c8* text) {
    if (h == NULL) {
        return 0;
    }

    return sw_h_append_escaped(h, sw_translate(h->translator, text), 0);
}

b8 sw_raw(sw_hbuf* h, const c8* text) {
    if (h == NULL) {
        return 0;
    }

    return sw_char_array_append_cstr(&h->bytes, text);
}

b8 sw_rawf(sw_hbuf* h, const c8* fmt, ...) {
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

b8 sw_title(sw_hbuf* h, const c8* text) {
    if (!sw_tag(h, "title", sw_no_attrs)) return 0;
    if (!sw_txt(h, text)) return 0;
    return sw_end(h, "title");
}

b8 sw_title_tr(sw_hbuf* h, const c8* text) {
    if (!sw_tag(h, "title", sw_no_attrs)) return 0;
    if (!sw_txt_tr(h, text)) return 0;
    return sw_end(h, "title");
}

b8 sw_meta_charset(sw_hbuf* h, const c8* charset) {
    return sw_void(h, "meta", sw_attrs(sw_kv("charset", charset)));
}
