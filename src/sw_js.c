#include "sw_internal.h"

static const c8* const sw_j_runtime_chunks[] = {
    "(function(){",
    "if(window.__swjsRuntime){return;}",
    "function ready(fn){if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',fn,{once:true});}else{fn();}}",
    "function byId(id){return id?document.getElementById(id):null;}",
    "function eventName(type){switch(type){case 1:return 'input';case 2:return 'change';case 3:return 'submit';default:return 'click';}}",
    "function setLoading(el,cls,on){if(el&&cls){el.classList[on?'add':'remove'](cls);}}",
    "function serializeForm(form){var params=new URLSearchParams();if(!form){return params;}new FormData(form).forEach(function(value,key){params.append(key,value);});return params;}",
    "function appendQuery(url,query){if(!query){return url;}return url+(url.indexOf('?')===-1?'?':'&')+query;}",
    "function replaceTarget(targetId,html,swapMode){var target=byId(targetId);if(!target){return;}if(swapMode===1){target.outerHTML=html;return;}target.innerHTML=html;}",
    "function request(cfg,state){var form=byId(cfg.formId);var input=byId(cfg.inputId);var params;var url=cfg.endpoint||'';var fetchOptions;var method;",
    "if(cfg.serializeForm){params=serializeForm(form);}else{params=new URLSearchParams();if(cfg.valueParam&&input){params.set(cfg.valueParam,input.value);}}",
    "if(cfg.abortStale&&state.controller){state.controller.abort();}",
    "state.requestId+=1;",
    "var currentId=state.requestId;",
    "state.controller=(typeof AbortController==='function')?new AbortController():null;",
    "setLoading(byId(cfg.targetId),cfg.loadingClass,true);",
    "method=(cfg.method===1)?'POST':'GET';",
    "fetchOptions={method:method,signal:state.controller?state.controller.signal:void 0};",
    "if(method==='GET'){url=appendQuery(url,params.toString());}else{fetchOptions.headers={'Content-Type':'application/x-www-form-urlencoded; charset=UTF-8'};fetchOptions.body=params.toString();}",
    "fetch(url,fetchOptions).then(function(response){if(!response.ok){throw new Error('sw_js request failed');}return response.text();})",
    ".then(function(html){if(currentId!==state.requestId){return;}replaceTarget(cfg.targetId,html,cfg.swapMode);})",
    ".catch(function(error){if(error&&error.name==='AbortError'){return;}console.error(error);})",
    ".finally(function(){if(currentId===state.requestId){setLoading(byId(cfg.targetId),cfg.loadingClass,false);}});}",
    "function bindEvent(element,type,handler){if(!element){return;}element.addEventListener(eventName(type),handler);}",
    "window.__swjsRuntime={",
    "liveSearch:function(cfg){ready(function(){var input=byId(cfg.inputId);var form=byId(cfg.formId);var state={controller:null,requestId:0,timer:0};",
    "function queueRequest(){window.clearTimeout(state.timer);if(cfg.debounceMs>0){state.timer=window.setTimeout(function(){request(cfg,state);},cfg.debounceMs);}else{request(cfg,state);}}",
    "if(!input||!byId(cfg.targetId)){return;}",
    "input.addEventListener('input',queueRequest);",
    "if(form&&cfg.preventSubmit){form.addEventListener('submit',function(event){event.preventDefault();window.clearTimeout(state.timer);request(cfg,state);});}",
    "});},",
    "fetchReplace:function(cfg){ready(function(){var trigger=byId(cfg.triggerId);var state={controller:null,requestId:0,timer:0};",
    "if(!trigger||!byId(cfg.targetId)){return;}",
    "bindEvent(trigger,cfg.eventType,function(event){if(cfg.preventDefault){event.preventDefault();}request(cfg,state);});",
    "});},",
    "toggle:function(cfg){ready(function(){var trigger=byId(cfg.triggerId);var target=byId(cfg.targetId);",
    "function applyFromState(){var active=!!trigger.checked;if(cfg.invert){active=!active;}target.hidden=!active;}",
    "if(!trigger||!target){return;}",
    "if(cfg.useTriggerChecked){if(cfg.syncInitialState){applyFromState();}bindEvent(trigger,cfg.eventType,function(event){if(cfg.preventDefault){event.preventDefault();}applyFromState();});return;}",
    "bindEvent(trigger,cfg.eventType,function(event){if(cfg.preventDefault){event.preventDefault();}target.hidden=!target.hidden;});",
    "});},",
    "classToggle:function(cfg){ready(function(){var trigger=byId(cfg.triggerId);var target=byId(cfg.targetId);",
    "function applyFromState(){var active=!!trigger.checked;if(cfg.invert){active=!active;}target.classList.toggle(cfg.className,active);}",
    "if(!trigger||!target||!cfg.className){return;}",
    "if(cfg.useTriggerChecked){if(cfg.syncInitialState){applyFromState();}bindEvent(trigger,cfg.eventType,function(event){if(cfg.preventDefault){event.preventDefault();}applyFromState();});return;}",
    "bindEvent(trigger,cfg.eventType,function(event){if(cfg.preventDefault){event.preventDefault();}target.classList.toggle(cfg.className);});",
    "});}",
    "};",
    "})();"
};

static b8 sw_j_append_string(sw_char_array* out, const c8* value) {
    sz i;
    char hex_escape[5];

    if (!sw_char_array_append_byte(out, '"')) {
        return 0;
    }

    if (value != NULL) {
        for (i = 0; value[i] != '\0'; ++i) {
            switch (value[i]) {
                case '\\':
                    if (!sw_char_array_append_cstr(out, "\\\\")) return 0;
                    break;
                case '"':
                    if (!sw_char_array_append_cstr(out, "\\\"")) return 0;
                    break;
                case '\'':
                    if (!sw_char_array_append_cstr(out, "\\x27")) return 0;
                    break;
                case '\n':
                    if (!sw_char_array_append_cstr(out, "\\n")) return 0;
                    break;
                case '\r':
                    if (!sw_char_array_append_cstr(out, "\\r")) return 0;
                    break;
                case '\t':
                    if (!sw_char_array_append_cstr(out, "\\t")) return 0;
                    break;
                case '\b':
                    if (!sw_char_array_append_cstr(out, "\\b")) return 0;
                    break;
                case '\f':
                    if (!sw_char_array_append_cstr(out, "\\f")) return 0;
                    break;
                case '<':
                    if (!sw_char_array_append_cstr(out, "\\x3C")) return 0;
                    break;
                default:
                    if ((unsigned char)value[i] < 0x20) {
                        snprintf(hex_escape, sizeof(hex_escape), "\\x%02X", (unsigned char)value[i]);
                        if (!sw_char_array_append_cstr(out, hex_escape)) return 0;
                    } else if (!sw_char_array_append_byte(out, value[i])) {
                        return 0;
                    }
                    break;
            }
        }
    }

    return sw_char_array_append_byte(out, '"');
}

static b8 sw_j_append_key(sw_char_array* out, const c8* key, b8* first) {
    if (!*first && !sw_char_array_append_byte(out, ',')) {
        return 0;
    }
    *first = 0;
    if (!sw_j_append_string(out, key)) {
        return 0;
    }
    return sw_char_array_append_byte(out, ':');
}

static b8 sw_j_append_string_field(sw_char_array* out, const c8* key, const c8* value, b8* first) {
    if (!sw_j_append_key(out, key, first)) {
        return 0;
    }
    if (value == NULL) {
        return sw_char_array_append_cstr(out, "null");
    }
    return sw_j_append_string(out, value);
}

static b8 sw_j_append_bool_field(sw_char_array* out, const c8* key, b8 value, b8* first) {
    if (!sw_j_append_key(out, key, first)) {
        return 0;
    }
    return sw_char_array_append_cstr(out, value ? "true" : "false");
}

static b8 sw_j_append_number_field(sw_char_array* out, const c8* key, i32 value, b8* first) {
    char number[32];

    if (!sw_j_append_key(out, key, first)) {
        return 0;
    }

    snprintf(number, sizeof(number), "%d", value);
    return sw_char_array_append_cstr(out, number);
}

static b8 sw_j_emit_initializer(sw_hbuf* h, const c8* helper_name, const c8* method_name, const sw_char_array* config) {
    if (!sw_j_runtime(h)) {
        return 0;
    }
    if (!sw_tag(h, "script", sw_attrs(sw_kv("data-swjs", helper_name)))) return 0;
    if (!sw_rawf(h, "window.__swjsRuntime.%s(", method_name)) return 0;
    if (!sw_raw(h, sw_char_array_data(config))) return 0;
    if (!sw_raw(h, ");")) return 0;
    return sw_end(h, "script");
}

static b8 sw_j_emit_live_config(sw_char_array* out, const sw_j_live_opts* opt) {
    b8 first = 1;

    if (!sw_char_array_append_byte(out, '{')) return 0;
    if (!sw_j_append_string_field(out, "formId", opt->form_id, &first)) return 0;
    if (!sw_j_append_string_field(out, "inputId", opt->input_id, &first)) return 0;
    if (!sw_j_append_string_field(out, "targetId", opt->target_id, &first)) return 0;
    if (!sw_j_append_string_field(out, "endpoint", opt->endpoint, &first)) return 0;
    if (!sw_j_append_string_field(out, "valueParam", opt->value_param != NULL ? opt->value_param : "value", &first)) return 0;
    if (!sw_j_append_string_field(out, "loadingClass", opt->loading_class, &first)) return 0;
    if (!sw_j_append_number_field(out, "debounceMs", opt->debounce_ms, &first)) return 0;
    if (!sw_j_append_number_field(out, "method", (i32)opt->method, &first)) return 0;
    if (!sw_j_append_number_field(out, "swapMode", (i32)opt->swap_mode, &first)) return 0;
    if (!sw_j_append_bool_field(out, "serializeForm", opt->serialize_form, &first)) return 0;
    if (!sw_j_append_bool_field(out, "abortStale", opt->abort_stale, &first)) return 0;
    if (!sw_j_append_bool_field(out, "preventSubmit", opt->prevent_submit, &first)) return 0;
    return sw_char_array_append_byte(out, '}');
}

static b8 sw_j_emit_fetch_config(sw_char_array* out, const sw_j_fetch_opts* opt) {
    b8 first = 1;

    if (!sw_char_array_append_byte(out, '{')) return 0;
    if (!sw_j_append_string_field(out, "triggerId", opt->trigger_id, &first)) return 0;
    if (!sw_j_append_string_field(out, "formId", opt->form_id, &first)) return 0;
    if (!sw_j_append_string_field(out, "inputId", NULL, &first)) return 0;
    if (!sw_j_append_string_field(out, "targetId", opt->target_id, &first)) return 0;
    if (!sw_j_append_string_field(out, "endpoint", opt->endpoint, &first)) return 0;
    if (!sw_j_append_string_field(out, "valueParam", NULL, &first)) return 0;
    if (!sw_j_append_string_field(out, "loadingClass", opt->loading_class, &first)) return 0;
    if (!sw_j_append_number_field(out, "eventType", (i32)opt->event_type, &first)) return 0;
    if (!sw_j_append_number_field(out, "method", (i32)opt->method, &first)) return 0;
    if (!sw_j_append_number_field(out, "swapMode", (i32)opt->swap_mode, &first)) return 0;
    if (!sw_j_append_bool_field(out, "serializeForm", opt->serialize_form, &first)) return 0;
    if (!sw_j_append_bool_field(out, "abortStale", opt->abort_stale, &first)) return 0;
    if (!sw_j_append_bool_field(out, "preventDefault", opt->prevent_default, &first)) return 0;
    return sw_char_array_append_byte(out, '}');
}

static b8 sw_j_emit_toggle_config(sw_char_array* out, const sw_j_toggle_opts* opt) {
    b8 first = 1;

    if (!sw_char_array_append_byte(out, '{')) return 0;
    if (!sw_j_append_string_field(out, "triggerId", opt->trigger_id, &first)) return 0;
    if (!sw_j_append_string_field(out, "targetId", opt->target_id, &first)) return 0;
    if (!sw_j_append_number_field(out, "eventType", (i32)opt->event_type, &first)) return 0;
    if (!sw_j_append_bool_field(out, "preventDefault", opt->prevent_default, &first)) return 0;
    if (!sw_j_append_bool_field(out, "syncInitialState", opt->sync_initial_state, &first)) return 0;
    if (!sw_j_append_bool_field(out, "useTriggerChecked", opt->use_trigger_checked, &first)) return 0;
    if (!sw_j_append_bool_field(out, "invert", opt->invert, &first)) return 0;
    return sw_char_array_append_byte(out, '}');
}

static b8 sw_j_emit_class_config(sw_char_array* out, const sw_j_class_opts* opt) {
    b8 first = 1;

    if (!sw_char_array_append_byte(out, '{')) return 0;
    if (!sw_j_append_string_field(out, "triggerId", opt->trigger_id, &first)) return 0;
    if (!sw_j_append_string_field(out, "targetId", opt->target_id, &first)) return 0;
    if (!sw_j_append_string_field(out, "className", opt->class_name, &first)) return 0;
    if (!sw_j_append_number_field(out, "eventType", (i32)opt->event_type, &first)) return 0;
    if (!sw_j_append_bool_field(out, "preventDefault", opt->prevent_default, &first)) return 0;
    if (!sw_j_append_bool_field(out, "syncInitialState", opt->sync_initial_state, &first)) return 0;
    if (!sw_j_append_bool_field(out, "useTriggerChecked", opt->use_trigger_checked, &first)) return 0;
    if (!sw_j_append_bool_field(out, "invert", opt->invert, &first)) return 0;
    return sw_char_array_append_byte(out, '}');
}

b8 sw_j_runtime(sw_hbuf* h) {
    sz i;

    if (h == NULL) {
        return 0;
    }
    if (h->js_runtime_emitted) {
        return 1;
    }

    if (!sw_tag(h, "script", sw_attrs(sw_kv("data-swjs", "runtime")))) {
        return 0;
    }
    for (i = 0; i < sizeof(sw_j_runtime_chunks) / sizeof(sw_j_runtime_chunks[0]); ++i) {
        if (!sw_raw(h, sw_j_runtime_chunks[i])) {
            return 0;
        }
    }
    if (!sw_end(h, "script")) {
        return 0;
    }
    h->js_runtime_emitted = 1;
    return 1;
}

b8 sw_j_live_cfg(sw_hbuf* h, const sw_j_live_opts* opt) {
    sw_char_array config;
    b8 ok;

    if (h == NULL || opt == NULL || opt->input_id == NULL || opt->target_id == NULL || opt->endpoint == NULL) {
        return 0;
    }

    sw_char_array_init(&config);
    ok = sw_j_emit_live_config(&config, opt)
        && sw_j_emit_initializer(h, "live-search", "liveSearch", &config);
    sw_char_array_free(&config);
    return ok;
}

b8 sw_j_fetch_cfg(sw_hbuf* h, const sw_j_fetch_opts* opt) {
    sw_char_array config;
    b8 ok;

    if (h == NULL || opt == NULL || opt->trigger_id == NULL || opt->target_id == NULL || opt->endpoint == NULL) {
        return 0;
    }

    sw_char_array_init(&config);
    ok = sw_j_emit_fetch_config(&config, opt)
        && sw_j_emit_initializer(h, "fetch-replace", "fetchReplace", &config);
    sw_char_array_free(&config);
    return ok;
}

b8 sw_j_toggle_cfg(sw_hbuf* h, const sw_j_toggle_opts* opt) {
    sw_char_array config;
    b8 ok;

    if (h == NULL || opt == NULL || opt->trigger_id == NULL || opt->target_id == NULL) {
        return 0;
    }

    sw_char_array_init(&config);
    ok = sw_j_emit_toggle_config(&config, opt)
        && sw_j_emit_initializer(h, "toggle", "toggle", &config);
    sw_char_array_free(&config);
    return ok;
}

b8 sw_j_class_cfg(sw_hbuf* h, const sw_j_class_opts* opt) {
    sw_char_array config;
    b8 ok;

    if (h == NULL || opt == NULL || opt->trigger_id == NULL || opt->target_id == NULL || opt->class_name == NULL) {
        return 0;
    }

    sw_char_array_init(&config);
    ok = sw_j_emit_class_config(&config, opt)
        && sw_j_emit_initializer(h, "class-toggle", "classToggle", &config);
    sw_char_array_free(&config);
    return ok;
}
