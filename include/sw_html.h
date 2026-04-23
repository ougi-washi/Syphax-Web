#ifndef SW_HTML_H
#define SW_HTML_H

#include "sw_export.h"
#include "sw_translator.h"
#include "syphax/s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sw_buffer sw_buffer;

typedef struct {
    const c8* name;
    const c8* value;
    b8 enabled;
    b8 no_translate;
    b8 is_boolean;
} sw_attr_item;

typedef struct {
    const sw_attr_item* items;
    sz count;
} sw_attr_list;

#define sw_attr(...) sw_attr_impl(__VA_ARGS__)
#define sw_attr_impl(_name, _value) ((sw_attr_item){ .name = (_name), .value = (_value), .enabled = 1 })
#define sw_attr_no_translate(...) sw_attr_no_translate_impl(__VA_ARGS__)
#define sw_attr_no_translate_impl(_name, _value) ((sw_attr_item){ .name = (_name), .value = (_value), .enabled = 1, .no_translate = 1 })
#define sw_attr_bool(_name, _enabled) ((sw_attr_item){ .name = (_name), .enabled = (_enabled), .is_boolean = 1 })
#define sw_translation(_enabled) "__sw_translation__", ((_enabled) ? "1" : "0")
#define sw_direction_value(_direction) (((_direction) == SW_LANGUAGE_DIRECTION_RTL) ? "rtl" : (((_direction) == SW_LANGUAGE_DIRECTION_TTB) ? "ttb" : "ltr"))
#define sw_direction(_direction) "__sw_direction__", sw_direction_value(_direction)
#define sw_attrs(...) sw_attrs_impl(((const sw_attr_item[]){ {0}, __VA_ARGS__ }), sizeof((const sw_attr_item[]){ {0}, __VA_ARGS__ }) / sizeof(sw_attr_item))
#define sw_attrs_impl(_items, _count) ((sw_attr_list){ .items = (_items) + 1, .count = (_count) - 1 })

SW_API sw_buffer* sw_buffer_new(void);
SW_API void sw_buffer_free(sw_buffer* buffer);
SW_API void sw_buffer_reset(sw_buffer* buffer);
SW_API void sw_buffer_set_translator(sw_buffer* buffer, const sw_translator* translator);
SW_API const sw_translator* sw_buffer_get_translator(const sw_buffer* buffer);
SW_API void sw_buffer_set_translation(sw_buffer* buffer, b8 enabled);
SW_API b8 sw_buffer_translation_enabled(const sw_buffer* buffer);
SW_API const c8* sw_buffer_data(const sw_buffer* buffer);
SW_API sz sw_buffer_len(const sw_buffer* buffer);

SW_API b8 sw_tag(sw_buffer* buffer, const c8* tag, sw_attr_list attrs);
SW_API b8 sw_end(sw_buffer* buffer, const c8* tag);
SW_API b8 sw_void(sw_buffer* buffer, const c8* tag, sw_attr_list attrs);
SW_API b8 sw_text(sw_buffer* buffer, const c8* text);
SW_API b8 sw_text_no_translate(sw_buffer* buffer, const c8* text);
SW_API b8 sw_raw(sw_buffer* buffer, const c8* text);
SW_API b8 sw_rawf(sw_buffer* buffer, const c8* fmt, ...);
SW_API b8 sw_title(sw_buffer* buffer, const c8* text);
SW_API b8 sw_title_no_translate(sw_buffer* buffer, const c8* text);
SW_API b8 sw_meta_charset(sw_buffer* buffer, const c8* charset);

#define sw_translate_on(_buffer) sw_buffer_set_translation((_buffer), 1)
#define sw_translate_off(_buffer) sw_buffer_set_translation((_buffer), 0)

#define SW_BLOCK_TAG(_buffer, _tag, _attrs, _content) \
    do { \
        if (sw_tag((_buffer), (_tag), _attrs)) { \
            _content \
            (void)sw_end((_buffer), (_tag)); \
        } \
    } while (0)

#define SW_VOID_TAG(_buffer, _tag, _attrs) \
    do { \
        (void)sw_void((_buffer), (_tag), _attrs); \
    } while (0)

#define sw_el(_buffer, _tag, _attrs, _content) SW_BLOCK_TAG((_buffer), (_tag), _attrs, _content)

#define sw_html(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "html", _attrs, _content)
#define sw_head(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "head", _attrs, _content)
#define sw_body(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "body", _attrs, _content)
#define sw_main(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "main", _attrs, _content)
#define sw_header(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "header", _attrs, _content)
#define sw_footer(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "footer", _attrs, _content)
#define sw_nav(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "nav", _attrs, _content)
#define sw_section(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "section", _attrs, _content)
#define sw_article(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "article", _attrs, _content)
#define sw_aside(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "aside", _attrs, _content)
#define sw_div(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "div", _attrs, _content)
#define sw_span(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "span", _attrs, _content)
#define sw_p(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "p", _attrs, _content)
#define sw_h1(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "h1", _attrs, _content)
#define sw_h2(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "h2", _attrs, _content)
#define sw_h3(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "h3", _attrs, _content)
#define sw_h4(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "h4", _attrs, _content)
#define sw_h5(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "h5", _attrs, _content)
#define sw_h6(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "h6", _attrs, _content)
#define sw_strong(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "strong", _attrs, _content)
#define sw_em(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "em", _attrs, _content)
#define sw_small(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "small", _attrs, _content)
#define sw_code(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "code", _attrs, _content)
#define sw_pre(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "pre", _attrs, _content)
#define sw_ul(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "ul", _attrs, _content)
#define sw_ol(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "ol", _attrs, _content)
#define sw_li(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "li", _attrs, _content)
#define sw_a(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "a", _attrs, _content)
#define sw_form(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "form", _attrs, _content)
#define sw_label(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "label", _attrs, _content)
#define sw_button(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "button", _attrs, _content)
#define sw_textarea(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "textarea", _attrs, _content)
#define sw_select(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "select", _attrs, _content)
#define sw_option(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "option", _attrs, _content)
#define sw_script(_buffer, _attrs, _content) SW_BLOCK_TAG((_buffer), "script", _attrs, _content)

#define sw_input(_buffer, _attrs) SW_VOID_TAG((_buffer), "input", _attrs)
#define sw_img(_buffer, _attrs) SW_VOID_TAG((_buffer), "img", _attrs)
#define sw_link(_buffer, _attrs) SW_VOID_TAG((_buffer), "link", _attrs)
#define sw_meta(_buffer, _attrs) SW_VOID_TAG((_buffer), "meta", _attrs)
#define sw_br(_buffer, _attrs) SW_VOID_TAG((_buffer), "br", _attrs)
#define sw_hr(_buffer, _attrs) SW_VOID_TAG((_buffer), "hr", _attrs)

#ifdef __cplusplus
}
#endif

#endif
