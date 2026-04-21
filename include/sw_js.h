#ifndef SW_JS_H
#define SW_JS_H

#include "sw_export.h"
#include "sw_html.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SW_JS_EVENT_CLICK = 0,
    SW_JS_EVENT_INPUT = 1,
    SW_JS_EVENT_CHANGE = 2,
    SW_JS_EVENT_SUBMIT = 3
} sw_js_event_type;

typedef enum {
    SW_JS_HTTP_GET = 0,
    SW_JS_HTTP_POST = 1
} sw_js_http_method;

typedef enum {
    SW_JS_SWAP_INNER_HTML = 0,
    SW_JS_SWAP_OUTER_HTML = 1
} sw_js_swap_mode;

typedef struct {
    const c8* form_id;
    const c8* input_id;
    const c8* target_id;
    const c8* endpoint;
    const c8* value_param;
    const c8* loading_class;
    i32 debounce_ms;
    sw_js_http_method method;
    sw_js_swap_mode swap_mode;
    b8 serialize_form;
    b8 abort_stale;
    b8 prevent_submit;
} sw_js_live_search_options;

typedef struct {
    const c8* trigger_id;
    const c8* form_id;
    const c8* target_id;
    const c8* endpoint;
    const c8* loading_class;
    sw_js_event_type event_type;
    sw_js_http_method method;
    sw_js_swap_mode swap_mode;
    b8 serialize_form;
    b8 abort_stale;
    b8 prevent_default;
} sw_js_fetch_replace_options;

typedef struct {
    const c8* trigger_id;
    const c8* target_id;
    sw_js_event_type event_type;
    b8 prevent_default;
    b8 sync_initial_state;
    b8 use_trigger_checked;
    b8 invert;
} sw_js_toggle_options;

typedef struct {
    const c8* trigger_id;
    const c8* target_id;
    const c8* class_name;
    sw_js_event_type event_type;
    b8 prevent_default;
    b8 sync_initial_state;
    b8 use_trigger_checked;
    b8 invert;
} sw_js_class_toggle_options;

SW_API b8 sw_js_emit_runtime(sw_html_buffer* html);
SW_API b8 sw_js_live_search(sw_html_buffer* html, const sw_js_live_search_options* options);
SW_API b8 sw_js_fetch_replace(sw_html_buffer* html, const sw_js_fetch_replace_options* options);
SW_API b8 sw_js_toggle(sw_html_buffer* html, const sw_js_toggle_options* options);
SW_API b8 sw_js_class_toggle(sw_html_buffer* html, const sw_js_class_toggle_options* options);

#ifdef __cplusplus
}
#endif

#endif
