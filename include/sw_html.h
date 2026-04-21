#ifndef SW_HTML_H
#define SW_HTML_H

#include "sw_export.h"
#include "sw_translator.h"
#include "syphax/s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sw_hbuf sw_hbuf;

typedef struct {
    const c8* name;
    const c8* value;
    b8 enabled;
    b8 translate;
    b8 is_boolean;
} sw_attr;

typedef struct {
    const sw_attr* items;
    sz count;
} sw_attr_list;

#define sw_kv(_name, _value) ((sw_attr){ .name = (_name), .value = (_value), .enabled = 1 })
#define sw_tr(_name, _value) ((sw_attr){ .name = (_name), .value = (_value), .enabled = 1, .translate = 1 })
#define sw_bool(_name, _enabled) ((sw_attr){ .name = (_name), .enabled = (_enabled), .is_boolean = 1 })
#define sw_attrs(...) ((sw_attr_list){ .items = (const sw_attr[]){ __VA_ARGS__ }, .count = sizeof((const sw_attr[]){ __VA_ARGS__ }) / sizeof(sw_attr) })
#define sw_no_attrs ((sw_attr_list){ NULL, 0 })

SW_API sw_hbuf* sw_hbuf_new(void);
SW_API void sw_hbuf_free(sw_hbuf* h);
SW_API void sw_hbuf_reset(sw_hbuf* h);
SW_API void sw_hbuf_set_tr(sw_hbuf* h, const sw_translator* tr);
SW_API const sw_translator* sw_hbuf_get_tr(const sw_hbuf* h);
SW_API const c8* sw_hbuf_data(const sw_hbuf* h);
SW_API sz sw_hbuf_len(const sw_hbuf* h);

SW_API b8 sw_tag(sw_hbuf* h, const c8* tag, sw_attr_list attrs);
SW_API b8 sw_end(sw_hbuf* h, const c8* tag);
SW_API b8 sw_void(sw_hbuf* h, const c8* tag, sw_attr_list attrs);
SW_API b8 sw_txt(sw_hbuf* h, const c8* text);
SW_API b8 sw_txt_tr(sw_hbuf* h, const c8* text);
SW_API b8 sw_raw(sw_hbuf* h, const c8* text);
SW_API b8 sw_rawf(sw_hbuf* h, const c8* fmt, ...);
SW_API b8 sw_title(sw_hbuf* h, const c8* text);
SW_API b8 sw_title_tr(sw_hbuf* h, const c8* text);
SW_API b8 sw_meta_charset(sw_hbuf* h, const c8* charset);

#define SW_BLOCK_TAG(_h, _tag, _attrs, _content) \
    do { \
        if (sw_tag((_h), (_tag), _attrs)) { \
            _content \
            (void)sw_end((_h), (_tag)); \
        } \
    } while (0)

#define SW_VOID_TAG(_h, _tag, _attrs) \
    do { \
        (void)sw_void((_h), (_tag), _attrs); \
    } while (0)

#define sw_el(_h, _tag, _attrs, _content) SW_BLOCK_TAG((_h), (_tag), _attrs, _content)

#define sw_html(_h, _attrs, _content) SW_BLOCK_TAG((_h), "html", _attrs, _content)
#define sw_head(_h, _attrs, _content) SW_BLOCK_TAG((_h), "head", _attrs, _content)
#define sw_body(_h, _attrs, _content) SW_BLOCK_TAG((_h), "body", _attrs, _content)
#define sw_main(_h, _attrs, _content) SW_BLOCK_TAG((_h), "main", _attrs, _content)
#define sw_header(_h, _attrs, _content) SW_BLOCK_TAG((_h), "header", _attrs, _content)
#define sw_footer(_h, _attrs, _content) SW_BLOCK_TAG((_h), "footer", _attrs, _content)
#define sw_nav(_h, _attrs, _content) SW_BLOCK_TAG((_h), "nav", _attrs, _content)
#define sw_section(_h, _attrs, _content) SW_BLOCK_TAG((_h), "section", _attrs, _content)
#define sw_article(_h, _attrs, _content) SW_BLOCK_TAG((_h), "article", _attrs, _content)
#define sw_aside(_h, _attrs, _content) SW_BLOCK_TAG((_h), "aside", _attrs, _content)
#define sw_div(_h, _attrs, _content) SW_BLOCK_TAG((_h), "div", _attrs, _content)
#define sw_span(_h, _attrs, _content) SW_BLOCK_TAG((_h), "span", _attrs, _content)
#define sw_p(_h, _attrs, _content) SW_BLOCK_TAG((_h), "p", _attrs, _content)
#define sw_h1(_h, _attrs, _content) SW_BLOCK_TAG((_h), "h1", _attrs, _content)
#define sw_h2(_h, _attrs, _content) SW_BLOCK_TAG((_h), "h2", _attrs, _content)
#define sw_h3(_h, _attrs, _content) SW_BLOCK_TAG((_h), "h3", _attrs, _content)
#define sw_h4(_h, _attrs, _content) SW_BLOCK_TAG((_h), "h4", _attrs, _content)
#define sw_h5(_h, _attrs, _content) SW_BLOCK_TAG((_h), "h5", _attrs, _content)
#define sw_h6(_h, _attrs, _content) SW_BLOCK_TAG((_h), "h6", _attrs, _content)
#define sw_strong(_h, _attrs, _content) SW_BLOCK_TAG((_h), "strong", _attrs, _content)
#define sw_em(_h, _attrs, _content) SW_BLOCK_TAG((_h), "em", _attrs, _content)
#define sw_small(_h, _attrs, _content) SW_BLOCK_TAG((_h), "small", _attrs, _content)
#define sw_code(_h, _attrs, _content) SW_BLOCK_TAG((_h), "code", _attrs, _content)
#define sw_pre(_h, _attrs, _content) SW_BLOCK_TAG((_h), "pre", _attrs, _content)
#define sw_ul(_h, _attrs, _content) SW_BLOCK_TAG((_h), "ul", _attrs, _content)
#define sw_ol(_h, _attrs, _content) SW_BLOCK_TAG((_h), "ol", _attrs, _content)
#define sw_li(_h, _attrs, _content) SW_BLOCK_TAG((_h), "li", _attrs, _content)
#define sw_a(_h, _attrs, _content) SW_BLOCK_TAG((_h), "a", _attrs, _content)
#define sw_form(_h, _attrs, _content) SW_BLOCK_TAG((_h), "form", _attrs, _content)
#define sw_label(_h, _attrs, _content) SW_BLOCK_TAG((_h), "label", _attrs, _content)
#define sw_button(_h, _attrs, _content) SW_BLOCK_TAG((_h), "button", _attrs, _content)
#define sw_textarea(_h, _attrs, _content) SW_BLOCK_TAG((_h), "textarea", _attrs, _content)
#define sw_select(_h, _attrs, _content) SW_BLOCK_TAG((_h), "select", _attrs, _content)
#define sw_option(_h, _attrs, _content) SW_BLOCK_TAG((_h), "option", _attrs, _content)
#define sw_script(_h, _attrs, _content) SW_BLOCK_TAG((_h), "script", _attrs, _content)

#define sw_input(_h, _attrs) SW_VOID_TAG((_h), "input", _attrs)
#define sw_img(_h, _attrs) SW_VOID_TAG((_h), "img", _attrs)
#define sw_link(_h, _attrs) SW_VOID_TAG((_h), "link", _attrs)
#define sw_meta(_h, _attrs) SW_VOID_TAG((_h), "meta", _attrs)
#define sw_br(_h, _attrs) SW_VOID_TAG((_h), "br", _attrs)
#define sw_hr(_h, _attrs) SW_VOID_TAG((_h), "hr", _attrs)

#ifdef __cplusplus
}
#endif

#endif
