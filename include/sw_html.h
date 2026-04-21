#ifndef SW_HTML_H
#define SW_HTML_H

#include "sw_export.h"
#include "sw_translator.h"
#include "syphax/s_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sw_html_buffer sw_html_buffer;

typedef struct {
    const c8* name;
    const c8* value;
    b8 enabled;
    b8 translate;
    b8 is_boolean;
} sw_html_attr_item;
#define sw_html_attr_kv(_name, _value) ((sw_html_attr_item){ .name = (_name), .value = (_value), .enabled = 1 })
#define sw_html_attr_kv_tr(_name, _value) ((sw_html_attr_item){ .name = (_name), .value = (_value), .enabled = 1, .translate = 1 })
#define sw_html_attr_bool(_name, _enabled) ((sw_html_attr_item){ .name = (_name), .enabled = (_enabled), .is_boolean = 1 })
#define sw_html_attr_items(...) ((const sw_html_attr_item[]){ __VA_ARGS__ }), (sizeof((const sw_html_attr_item[]){ __VA_ARGS__ }) / sizeof(sw_html_attr_item))

SW_API sw_html_buffer* sw_html_buffer_create(void);
SW_API void sw_html_buffer_destroy(sw_html_buffer* buffer);
SW_API void sw_html_buffer_clear(sw_html_buffer* buffer);
SW_API void sw_html_buffer_set_translator(sw_html_buffer* buffer, const sw_translator* translator);
SW_API const sw_translator* sw_html_buffer_get_translator(const sw_html_buffer* buffer);
SW_API const c8* sw_html_buffer_data(const sw_html_buffer* buffer);
SW_API sz sw_html_buffer_size(const sw_html_buffer* buffer);

SW_API b8 sw_html_open_tag(sw_html_buffer* buffer, const c8* tag, const sw_html_attr_item* attrs, sz attr_count);
SW_API b8 sw_html_close_tag(sw_html_buffer* buffer, const c8* tag);
SW_API b8 sw_html_void_tag(sw_html_buffer* buffer, const c8* tag, const sw_html_attr_item* attrs, sz attr_count);
SW_API b8 sw_html_text(sw_html_buffer* buffer, const c8* text);
SW_API b8 sw_html_text_tr(sw_html_buffer* buffer, const c8* text);
SW_API b8 sw_html_raw(sw_html_buffer* buffer, const c8* text);
SW_API b8 sw_html_rawf(sw_html_buffer* buffer, const c8* fmt, ...);
SW_API b8 sw_html_title(sw_html_buffer* buffer, const c8* text);
SW_API b8 sw_html_title_tr(sw_html_buffer* buffer, const c8* text);
SW_API b8 sw_html_meta_charset(sw_html_buffer* buffer, const c8* charset);

#ifdef __cplusplus
}
#endif

#endif
