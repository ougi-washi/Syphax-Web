#ifndef SW_HTML_H
#define SW_HTML_H

#include "syphax/web/sw_export.h"
#include "syphax/web/sw_translator.h"
#include "syphax/web/sw_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sw_html_buffer sw_html_buffer;

typedef struct {
    const c8* id;
    const c8* class_name;
    const c8* name;
    const c8* rel;
    const c8* placeholder;
    const c8* type;
    const c8* value;
    const c8* enctype;
    const c8* label_for;
    const c8* method;
    const c8* action;
    const c8* rows;
    const c8* cols;
    const c8* href;
    const c8* target;
    const c8* src;
    const c8* onclick;
    const c8* width;
    const c8* height;
    const c8* frameborder;
    const c8* charset;
    const c8* content;
    const c8* lang;
    const c8* title;
    b8 checked;
    b8 controls;
    b8 hidden;
    b8 defer_script;
    b8 async_script;
} sw_html_tag_attributes;

#define sw_html_attr(...) ((sw_html_tag_attributes){ __VA_ARGS__ })

SW_API sw_html_buffer* sw_html_buffer_create(void);
SW_API void sw_html_buffer_destroy(sw_html_buffer* buffer);
SW_API void sw_html_buffer_clear(sw_html_buffer* buffer);
SW_API void sw_html_buffer_set_translator(sw_html_buffer* buffer, const sw_translator* translator);
SW_API const sw_translator* sw_html_buffer_get_translator(const sw_html_buffer* buffer);
SW_API const c8* sw_html_buffer_data(const sw_html_buffer* buffer);
SW_API sz sw_html_buffer_size(const sw_html_buffer* buffer);

SW_API b8 sw_html_open_tag(sw_html_buffer* buffer, const c8* tag, const sw_html_tag_attributes* attrs);
SW_API b8 sw_html_close_tag(sw_html_buffer* buffer, const c8* tag);
SW_API b8 sw_html_void_tag(sw_html_buffer* buffer, const c8* tag, const sw_html_tag_attributes* attrs);
SW_API b8 sw_html_text(sw_html_buffer* buffer, const c8* text);
SW_API b8 sw_html_raw(sw_html_buffer* buffer, const c8* text);
SW_API b8 sw_html_rawf(sw_html_buffer* buffer, const c8* fmt, ...);
SW_API b8 sw_html_title(sw_html_buffer* buffer, const c8* text);
SW_API b8 sw_html_meta_charset(sw_html_buffer* buffer, const c8* charset);

#ifdef __cplusplus
}
#endif

#endif
