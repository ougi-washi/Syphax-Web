#ifndef SW_JS_H
#define SW_JS_H

#include "sw_export.h"
#include "sw_html.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    SW_J_CLICK = 0,
    SW_J_INPUT = 1,
    SW_J_CHANGE = 2,
    SW_J_SUBMIT = 3
} sw_j_event;

typedef enum {
    SW_J_GET = 0,
    SW_J_POST = 1
} sw_j_method;

typedef enum {
    SW_J_INNER = 0,
    SW_J_OUTER = 1
} sw_j_swap;

typedef struct {
    const c8* form_id;
    const c8* input_id;
    const c8* target_id;
    const c8* endpoint;
    const c8* value_param;
    const c8* loading_class;
    i32 debounce_ms;
    sw_j_method method;
    sw_j_swap swap_mode;
    b8 serialize_form;
    b8 abort_stale;
    b8 prevent_submit;
} sw_j_live_opts;

typedef struct {
    const c8* trigger_id;
    const c8* form_id;
    const c8* target_id;
    const c8* endpoint;
    const c8* loading_class;
    sw_j_event event_type;
    sw_j_method method;
    sw_j_swap swap_mode;
    b8 serialize_form;
    b8 abort_stale;
    b8 prevent_default;
} sw_j_fetch_opts;

typedef struct {
    const c8* trigger_id;
    const c8* target_id;
    sw_j_event event_type;
    b8 prevent_default;
    b8 sync_initial_state;
    b8 use_trigger_checked;
    b8 invert;
} sw_j_toggle_opts;

typedef struct {
    const c8* trigger_id;
    const c8* target_id;
    const c8* class_name;
    sw_j_event event_type;
    b8 prevent_default;
    b8 sync_initial_state;
    b8 use_trigger_checked;
    b8 invert;
} sw_j_class_opts;

SW_API b8 sw_j_runtime(sw_hbuf* h);
SW_API b8 sw_j_live_search(sw_hbuf* h, const c8* form_id, const c8* input_id, const c8* target_id, const c8* endpoint);
SW_API b8 sw_j_live_cfg(sw_hbuf* h, const sw_j_live_opts* opt);
SW_API b8 sw_j_fetch_cfg(sw_hbuf* h, const sw_j_fetch_opts* opt);
SW_API b8 sw_j_toggle_cfg(sw_hbuf* h, const sw_j_toggle_opts* opt);
SW_API b8 sw_j_class_cfg(sw_hbuf* h, const sw_j_class_opts* opt);

#define sw_j_live(_h, ...) sw_j_live_cfg((_h), &(sw_j_live_opts){ __VA_ARGS__ })
#define sw_j_fetch(_h, ...) sw_j_fetch_cfg((_h), &(sw_j_fetch_opts){ __VA_ARGS__ })
#define sw_j_toggle(_h, ...) sw_j_toggle_cfg((_h), &(sw_j_toggle_opts){ __VA_ARGS__ })
#define sw_j_class(_h, ...) sw_j_class_cfg((_h), &(sw_j_class_opts){ __VA_ARGS__ })

#ifdef __cplusplus
}
#endif

#endif
