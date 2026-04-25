#ifndef SW_JS_H
#define SW_JS_H

#include "sw_export.h"
#include "sw_html.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SW_JS_CLICK = 0,
    SW_JS_INPUT = 1,
    SW_JS_CHANGE = 2,
    SW_JS_SUBMIT = 3
} sw_js_event;

typedef enum {
    SW_JS_GET = 0,
    SW_JS_POST = 1
} sw_js_method;

typedef enum {
    SW_JS_INNER = 0,
    SW_JS_OUTER = 1
} sw_js_swap;

typedef struct {
    const c8* form_id;
    const c8* input_id;
    const c8* target_id;
    const c8* endpoint;
    const c8* value_param;
    const c8* loading_class;
    i32 debounce_ms;
    sw_js_method method;
    sw_js_swap swap_mode;
    b8 serialize_form;
    b8 abort_stale;
    b8 prevent_submit;
} sw_js_live_opts;

typedef struct {
    const c8* trigger_id;
    const c8* form_id;
    const c8* target_id;
    const c8* endpoint;
    const c8* loading_class;
    sw_js_event event_type;
    sw_js_method method;
    sw_js_swap swap_mode;
    b8 serialize_form;
    b8 abort_stale;
    b8 prevent_default;
} sw_js_fetch_opts;

typedef struct {
    const c8* trigger_id;
    const c8* target_id;
    sw_js_event event_type;
    b8 prevent_default;
    b8 sync_initial_state;
    b8 use_trigger_checked;
    b8 invert;
} sw_js_toggle_opts;

typedef struct {
    const c8* trigger_id;
    const c8* target_id;
    const c8* class_name;
    sw_js_event event_type;
    b8 prevent_default;
    b8 sync_initial_state;
    b8 use_trigger_checked;
    b8 invert;
} sw_js_class_opts;

SW_API b8 sw_js_runtime(sw_buffer* buffer);
SW_API b8 sw_js_live_search(sw_buffer* buffer, const c8* form_id, const c8* input_id, const c8* target_id, const c8* endpoint);
SW_API b8 (sw_js_live)(sw_buffer* buffer, const sw_js_live_opts* opt);
SW_API b8 (sw_js_fetch)(sw_buffer* buffer, const sw_js_fetch_opts* opt);
SW_API b8 (sw_js_toggle)(sw_buffer* buffer, const sw_js_toggle_opts* opt);
SW_API b8 (sw_js_class)(sw_buffer* buffer, const sw_js_class_opts* opt);

#define sw_js_live(_buffer, ...) (sw_js_live)((_buffer), &(sw_js_live_opts){ __VA_ARGS__ })
#define sw_js_fetch(_buffer, ...) (sw_js_fetch)((_buffer), &(sw_js_fetch_opts){ __VA_ARGS__ })
#define sw_js_toggle(_buffer, ...) (sw_js_toggle)((_buffer), &(sw_js_toggle_opts){ __VA_ARGS__ })
#define sw_js_class(_buffer, ...) (sw_js_class)((_buffer), &(sw_js_class_opts){ __VA_ARGS__ })

#ifdef __cplusplus
}
#endif

#endif
