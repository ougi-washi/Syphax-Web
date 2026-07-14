#include "sw_js.h"
#include "sw_internal.h"

#include <stdio.h>

static const c8* const sw_js_runtime_chunks[] = {
    "(function () {",
    "if (window.__swjsRuntime) { return; }",

    "function whenDocumentReady(callback) {"
        "if (document.readyState === 'loading') {"
            "document.addEventListener('DOMContentLoaded', callback, { once: true });"
        "} else {"
            "callback();"
        "}"
    "}",

    "function findElementById(elementId) {"
        "return elementId ? document.getElementById(elementId) : null;"
    "}",

    "function getEventName(eventType) {"
        "switch (eventType) {"
            "case 1: return 'input';"
            "case 2: return 'change';"
            "case 3: return 'submit';"
            "default: return 'click';"
        "}"
    "}",

    "function setLoadingClass(element, className, isLoading) {"
        "if (element && className) {"
            "element.classList[isLoading ? 'add' : 'remove'](className);"
        "}"
    "}",

    "function emitRequestState(targetElement, stateName, requestError) {"
        "if (!targetElement) { return; }"
        "targetElement.setAttribute('data-sw-state', stateName);"
        "if (stateName === 'loading') {"
            "targetElement.setAttribute('aria-busy', 'true');"
        "} else {"
            "targetElement.setAttribute('aria-busy', 'false');"
        "}"
        "if (stateName === 'error') {"
            "targetElement.setAttribute('data-sw-error', 'true');"
        "} else {"
            "targetElement.removeAttribute('data-sw-error');"
        "}"
        "if (typeof CustomEvent === 'function') {"
            "targetElement.dispatchEvent(new CustomEvent('sw:' + stateName, {"
                "detail: requestError ? { error: requestError } : void 0"
            "}));"
        "}"
    "}",

    "function serializeForm(formElement) {"
        "var parameters = new URLSearchParams();"
        "if (!formElement) { return parameters; }"
        "new FormData(formElement).forEach(function (value, key) {"
            "parameters.append(key, value);"
        "});"
        "return parameters;"
    "}",

    "function appendQueryString(url, queryString) {"
        "if (!queryString) { return url; }"
        "return url + (url.indexOf('?') === -1 ? '?' : '&') + queryString;"
    "}",

    "function restoreElementFocus(activeElementId) {"
        "var element;"
        "if (!activeElementId) { return; }"
        "element = findElementById(activeElementId);"
        "if (!element || typeof element.focus !== 'function') { return; }"
        "try {"
            "element.focus({ preventScroll: true });"
        "} catch (_) {"
            "element.focus();"
        "}"
    "}",

    "function replaceTargetContent(targetId, htmlContent, swapMode) {"
        "var targetElement = findElementById(targetId);"
        "var activeElement = document.activeElement;"
        "var activeElementId = activeElement && activeElement.id ? activeElement.id : null;"
        "var shouldRestoreFocus = !!(targetElement && activeElement"
            " && (activeElement === targetElement || targetElement.contains(activeElement)));"
        "if (!targetElement) { return; }"
        "if (swapMode === 1) {"
            "targetElement.outerHTML = htmlContent;"
        "} else {"
            "targetElement.innerHTML = htmlContent;"
        "}"
        "if (shouldRestoreFocus) { restoreElementFocus(activeElementId); }"
    "}",

    "function sendRequest(config, requestState) {"
        "var formElement = findElementById(config.formId);"
        "var inputElement = findElementById(config.inputId);"
        "var parameters;"
        "var requestUrl = config.endpoint || '';"
        "var requestOptions;"
        "var requestMethod;"
        "if (config.serializeForm) {"
            "parameters = serializeForm(formElement);"
        "} else {"
            "parameters = new URLSearchParams();"
            "if (config.valueParam && inputElement) {"
                "parameters.set(config.valueParam, inputElement.value);"
            "}"
        "}"
        "if (config.abortStale && requestState.controller) {"
            "requestState.controller.abort();"
        "}"
        "requestState.requestId += 1;"
        "var currentRequestId = requestState.requestId;"
        "var targetElement = findElementById(config.targetId);"
        "requestState.controller = typeof AbortController === 'function'"
            " ? new AbortController() : null;"
        "setLoadingClass(targetElement, config.loadingClass, true);"
        "emitRequestState(targetElement, 'loading');"
        "requestMethod = config.method === 1 ? 'POST' : 'GET';"
        "requestOptions = {"
            "method: requestMethod,"
            "signal: requestState.controller ? requestState.controller.signal : void 0"
        "};"
        "if (requestMethod === 'GET') {"
            "requestUrl = appendQueryString(requestUrl, parameters.toString());"
        "} else {"
            "requestOptions.headers = {"
                "'Content-Type': 'application/x-www-form-urlencoded; charset=UTF-8'"
            "};"
            "requestOptions.body = parameters.toString();"
        "}"
        "fetch(requestUrl, requestOptions)"
            ".then(function (response) {"
                "if (!response.ok) { throw new Error('sw_js request failed'); }"
                "return response.text();"
            "})"
            ".then(function (htmlContent) {"
                "if (currentRequestId !== requestState.requestId) { return; }"
                "replaceTargetContent(config.targetId, htmlContent, config.swapMode);"
                "emitRequestState(findElementById(config.targetId), 'success');"
            "})"
            ".catch(function (requestError) {"
                "if (requestError && requestError.name === 'AbortError') { return; }"
                "console.error(requestError);"
                "emitRequestState(findElementById(config.targetId), 'error', requestError);"
            "})"
            ".finally(function () {"
                "var currentTargetElement;"
                "if (currentRequestId === requestState.requestId) {"
                    "currentTargetElement = findElementById(config.targetId);"
                    "setLoadingClass(currentTargetElement, config.loadingClass, false);"
                    "if (currentTargetElement"
                        " && currentTargetElement.getAttribute('data-sw-state') === 'loading') {"
                        "emitRequestState(currentTargetElement, 'idle');"
                    "}"
                "}"
            "});"
    "}",

    "function bindConfiguredEvent(element, eventType, handler) {"
        "if (!element) { return; }"
        "element.addEventListener(getEventName(eventType), handler);"
    "}",

    "function requestDialogClose(dialogElement, returnValue) {"
        "var cancelEvent;"
        "if (!dialogElement || !dialogElement.open) { return; }"
        "if (typeof dialogElement.requestClose === 'function') {"
            "dialogElement.requestClose(returnValue || '');"
            "return;"
        "}"
        "if (typeof dialogElement.close !== 'function') { return; }"
        "cancelEvent = new Event('cancel', { cancelable: true });"
        "if (dialogElement.dispatchEvent(cancelEvent)) {"
            "dialogElement.close(returnValue || '');"
        "}"
    "}",

    "function isOutsideDialog(dialogElement, event) {"
        "var bounds = dialogElement.getBoundingClientRect();"
        "return event.clientX < bounds.left || event.clientX > bounds.right"
            " || event.clientY < bounds.top || event.clientY > bounds.bottom;"
    "}",

    "function enableBackdropClose(dialogElement) {"
        "var pointerDownOutside = false;"
        "if (dialogElement.__swjsBackdropClose) { return; }"
        "dialogElement.__swjsBackdropClose = true;"
        "dialogElement.addEventListener('pointerdown', function (event) {"
            "pointerDownOutside = event.target === dialogElement"
                " && isOutsideDialog(dialogElement, event);"
        "});"
        "dialogElement.addEventListener('click', function (event) {"
            "var shouldClose = pointerDownOutside && event.target === dialogElement"
                " && isOutsideDialog(dialogElement, event);"
            "pointerDownOutside = false;"
            "if (shouldClose) { requestDialogClose(dialogElement, ''); }"
        "});"
    "}",

    "function runModalAction(config) {"
        "var dialogElement = findElementById(config.targetId);"
        "var returnValue = config.returnValue || '';"
        "if (!dialogElement) { return; }"
        "if (config.closeOnBackdrop) { enableBackdropClose(dialogElement); }"
        "if (config.action === 1) {"
            "if (dialogElement.open && typeof dialogElement.close === 'function') {"
                "dialogElement.close(returnValue);"
            "}"
            "return;"
        "}"
        "if (config.action === 2) {"
            "requestDialogClose(dialogElement, returnValue);"
            "return;"
        "}"
        "if (dialogElement.open || typeof dialogElement.showModal !== 'function') { return; }"
        "dialogElement.returnValue = '';"
        "dialogElement.showModal();"
    "}",

    "window.__swjsRuntime = {"
        "liveSearch: function (config) {"
            "whenDocumentReady(function () {"
                "var inputElement = findElementById(config.inputId);"
                "var formElement = findElementById(config.formId);"
                "var requestState = { controller: null, requestId: 0, timer: 0 };"
                "function scheduleRequest() {"
                    "window.clearTimeout(requestState.timer);"
                    "if (config.debounceMs > 0) {"
                        "requestState.timer = window.setTimeout(function () {"
                            "sendRequest(config, requestState);"
                        "}, config.debounceMs);"
                    "} else {"
                        "sendRequest(config, requestState);"
                    "}"
                "}"
                "if (!inputElement || !findElementById(config.targetId)) { return; }"
                "inputElement.addEventListener('input', scheduleRequest);"
                "if (formElement && config.preventSubmit) {"
                    "formElement.addEventListener('submit', function (event) {"
                        "event.preventDefault();"
                        "window.clearTimeout(requestState.timer);"
                        "sendRequest(config, requestState);"
                    "});"
                "}"
            "});"
        "},"

        "fetchReplace: function (config) {"
            "whenDocumentReady(function () {"
                "var triggerElement = findElementById(config.triggerId);"
                "var requestState = { controller: null, requestId: 0, timer: 0 };"
                "if (!triggerElement || !findElementById(config.targetId)) { return; }"
                "bindConfiguredEvent(triggerElement, config.eventType, function (event) {"
                    "if (config.preventDefault) { event.preventDefault(); }"
                    "sendRequest(config, requestState);"
                "});"
            "});"
        "},"

        "toggle: function (config) {"
            "whenDocumentReady(function () {"
                "var triggerElement = findElementById(config.triggerId);"
                "var targetElement = findElementById(config.targetId);"
                "function applyCheckedState() {"
                    "var isActive = !!triggerElement.checked;"
                    "if (config.invert) { isActive = !isActive; }"
                    "targetElement.hidden = !isActive;"
                "}"
                "if (!triggerElement || !targetElement) { return; }"
                "if (config.useTriggerChecked) {"
                    "if (config.syncInitialState) { applyCheckedState(); }"
                    "bindConfiguredEvent(triggerElement, config.eventType, function (event) {"
                        "if (config.preventDefault) { event.preventDefault(); }"
                        "applyCheckedState();"
                    "});"
                    "return;"
                "}"
                "bindConfiguredEvent(triggerElement, config.eventType, function (event) {"
                    "if (config.preventDefault) { event.preventDefault(); }"
                    "targetElement.hidden = !targetElement.hidden;"
                "});"
            "});"
        "},"

        "classToggle: function (config) {"
            "whenDocumentReady(function () {"
                "var triggerElement = findElementById(config.triggerId);"
                "var targetElement = findElementById(config.targetId);"
                "function applyCheckedState() {"
                    "var isActive = !!triggerElement.checked;"
                    "if (config.invert) { isActive = !isActive; }"
                    "targetElement.classList.toggle(config.className, isActive);"
                "}"
                "if (!triggerElement || !targetElement || !config.className) { return; }"
                "if (config.useTriggerChecked) {"
                    "if (config.syncInitialState) { applyCheckedState(); }"
                    "bindConfiguredEvent(triggerElement, config.eventType, function (event) {"
                        "if (config.preventDefault) { event.preventDefault(); }"
                        "applyCheckedState();"
                    "});"
                    "return;"
                "}"
                "bindConfiguredEvent(triggerElement, config.eventType, function (event) {"
                    "if (config.preventDefault) { event.preventDefault(); }"
                    "targetElement.classList.toggle(config.className);"
                "});"
            "});"
        "},"

        "modal: function (config) {"
            "whenDocumentReady(function () {"
                "var triggerElement = findElementById(config.triggerId);"
                "if (!triggerElement) { return; }"
                "bindConfiguredEvent(triggerElement, config.eventType, function (event) {"
                    "if (config.preventDefault) { event.preventDefault(); }"
                    "runModalAction(config);"
                "});"
            "});"
        "}"
    "};",

    "})();"
};

static b8 sw_js_append_string(sw_char_array* out, const c8* value) {
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

static b8 sw_js_append_key(sw_char_array* out, const c8* key, b8* first) {
    if (!*first && !sw_char_array_append_cstr(out, ", ")) {
        return 0;
    }
    *first = 0;
    if (!sw_js_append_string(out, key)) {
        return 0;
    }
    return sw_char_array_append_cstr(out, ": ");
}

static b8 sw_js_append_string_field(sw_char_array* out, const c8* key, const c8* value, b8* first) {
    if (!sw_js_append_key(out, key, first)) {
        return 0;
    }
    if (value == NULL) {
        return sw_char_array_append_cstr(out, "null");
    }
    return sw_js_append_string(out, value);
}

static b8 sw_js_append_bool_field(sw_char_array* out, const c8* key, b8 value, b8* first) {
    if (!sw_js_append_key(out, key, first)) {
        return 0;
    }
    return sw_char_array_append_cstr(out, value ? "true" : "false");
}

static b8 sw_js_append_number_field(sw_char_array* out, const c8* key, i32 value, b8* first) {
    char number[32];

    if (!sw_js_append_key(out, key, first)) {
        return 0;
    }

    snprintf(number, sizeof(number), "%d", value);
    return sw_char_array_append_cstr(out, number);
}

static b8 sw_js_emit_initializer(sw_buffer* h, const c8* helper_name, const c8* method_name, const sw_char_array* config) {
    if (!sw_js_runtime(h)) {
        return 0;
    }
    if (!sw_tag(h, "script", sw_attrs(sw_attr("data-swjs", helper_name)))) return 0;
    if (!sw_rawf(h, "window.__swjsRuntime.%s(", method_name)) return 0;
    if (!sw_raw(h, sw_char_array_data(config))) return 0;
    if (!sw_raw(h, ");")) return 0;
    return sw_end(h, "script");
}

static b8 sw_js_emit_live_config(sw_char_array* out, const sw_js_live_opts* opt) {
    b8 first = 1;

    if (!sw_char_array_append_byte(out, '{')) return 0;
    if (!sw_js_append_string_field(out, "formId", opt->form_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "inputId", opt->input_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "targetId", opt->target_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "endpoint", opt->endpoint, &first)) return 0;
    if (!sw_js_append_string_field(out, "valueParam", opt->value_param != NULL ? opt->value_param : "value", &first)) return 0;
    if (!sw_js_append_string_field(out, "loadingClass", opt->loading_class, &first)) return 0;
    if (!sw_js_append_number_field(out, "debounceMs", opt->debounce_ms, &first)) return 0;
    if (!sw_js_append_number_field(out, "method", (i32)opt->method, &first)) return 0;
    if (!sw_js_append_number_field(out, "swapMode", (i32)opt->swap_mode, &first)) return 0;
    if (!sw_js_append_bool_field(out, "serializeForm", opt->serialize_form, &first)) return 0;
    if (!sw_js_append_bool_field(out, "abortStale", opt->abort_stale, &first)) return 0;
    if (!sw_js_append_bool_field(out, "preventSubmit", opt->prevent_submit, &first)) return 0;
    return sw_char_array_append_byte(out, '}');
}

static b8 sw_js_emit_fetch_config(sw_char_array* out, const sw_js_fetch_opts* opt) {
    b8 first = 1;

    if (!sw_char_array_append_byte(out, '{')) return 0;
    if (!sw_js_append_string_field(out, "triggerId", opt->trigger_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "formId", opt->form_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "inputId", NULL, &first)) return 0;
    if (!sw_js_append_string_field(out, "targetId", opt->target_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "endpoint", opt->endpoint, &first)) return 0;
    if (!sw_js_append_string_field(out, "valueParam", NULL, &first)) return 0;
    if (!sw_js_append_string_field(out, "loadingClass", opt->loading_class, &first)) return 0;
    if (!sw_js_append_number_field(out, "eventType", (i32)opt->event_type, &first)) return 0;
    if (!sw_js_append_number_field(out, "method", (i32)opt->method, &first)) return 0;
    if (!sw_js_append_number_field(out, "swapMode", (i32)opt->swap_mode, &first)) return 0;
    if (!sw_js_append_bool_field(out, "serializeForm", opt->serialize_form, &first)) return 0;
    if (!sw_js_append_bool_field(out, "abortStale", opt->abort_stale, &first)) return 0;
    if (!sw_js_append_bool_field(out, "preventDefault", opt->prevent_default, &first)) return 0;
    return sw_char_array_append_byte(out, '}');
}

static b8 sw_js_emit_toggle_config(sw_char_array* out, const sw_js_toggle_opts* opt) {
    b8 first = 1;

    if (!sw_char_array_append_byte(out, '{')) return 0;
    if (!sw_js_append_string_field(out, "triggerId", opt->trigger_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "targetId", opt->target_id, &first)) return 0;
    if (!sw_js_append_number_field(out, "eventType", (i32)opt->event_type, &first)) return 0;
    if (!sw_js_append_bool_field(out, "preventDefault", opt->prevent_default, &first)) return 0;
    if (!sw_js_append_bool_field(out, "syncInitialState", opt->sync_initial_state, &first)) return 0;
    if (!sw_js_append_bool_field(out, "useTriggerChecked", opt->use_trigger_checked, &first)) return 0;
    if (!sw_js_append_bool_field(out, "invert", opt->invert, &first)) return 0;
    return sw_char_array_append_byte(out, '}');
}

static b8 sw_js_emit_class_config(sw_char_array* out, const sw_js_class_opts* opt) {
    b8 first = 1;

    if (!sw_char_array_append_byte(out, '{')) return 0;
    if (!sw_js_append_string_field(out, "triggerId", opt->trigger_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "targetId", opt->target_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "className", opt->class_name, &first)) return 0;
    if (!sw_js_append_number_field(out, "eventType", (i32)opt->event_type, &first)) return 0;
    if (!sw_js_append_bool_field(out, "preventDefault", opt->prevent_default, &first)) return 0;
    if (!sw_js_append_bool_field(out, "syncInitialState", opt->sync_initial_state, &first)) return 0;
    if (!sw_js_append_bool_field(out, "useTriggerChecked", opt->use_trigger_checked, &first)) return 0;
    if (!sw_js_append_bool_field(out, "invert", opt->invert, &first)) return 0;
    return sw_char_array_append_byte(out, '}');
}

static b8 sw_js_emit_modal_config(sw_char_array* out, const sw_js_modal_opts* opt) {
    b8 first = 1;

    if (!sw_char_array_append_byte(out, '{')) return 0;
    if (!sw_js_append_string_field(out, "triggerId", opt->trigger_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "targetId", opt->target_id, &first)) return 0;
    if (!sw_js_append_string_field(out, "returnValue", opt->return_value, &first)) return 0;
    if (!sw_js_append_number_field(out, "eventType", (i32)opt->event_type, &first)) return 0;
    if (!sw_js_append_number_field(out, "action", (i32)opt->action, &first)) return 0;
    if (!sw_js_append_bool_field(out, "preventDefault", opt->prevent_default, &first)) return 0;
    if (!sw_js_append_bool_field(out, "closeOnBackdrop", opt->close_on_backdrop, &first)) return 0;
    return sw_char_array_append_byte(out, '}');
}

b8 sw_js_runtime(sw_buffer* h) {
    sz i;

    if (h == NULL) {
        return 0;
    }
    if (h->js_runtime_emitted) {
        return 1;
    }

    if (!sw_tag(h, "script", sw_attrs(sw_attr("data-swjs", "runtime")))) {
        return 0;
    }
    for (i = 0; i < sizeof(sw_js_runtime_chunks) / sizeof(sw_js_runtime_chunks[0]); ++i) {
        if (!sw_raw(h, sw_js_runtime_chunks[i])) {
            return 0;
        }
    }
    if (!sw_end(h, "script")) {
        return 0;
    }
    h->js_runtime_emitted = 1;
    return 1;
}

b8 sw_js_live_search(sw_buffer* h, const c8* form_id, const c8* input_id, const c8* target_id, const c8* endpoint) {
    const sw_js_live_opts options = {
        .form_id = form_id,
        .input_id = input_id,
        .target_id = target_id,
        .endpoint = endpoint,
        .loading_class = "is-loading",
        .debounce_ms = 120,
        .method = SW_JS_GET,
        .swap_mode = SW_JS_INNER,
        .serialize_form = 1,
        .abort_stale = 1,
        .prevent_submit = 1
    };

    return (sw_js_live)(h, &options);
}

b8 (sw_js_live)(sw_buffer* h, const sw_js_live_opts* opt) {
    sw_char_array config;
    b8 ok;

    if (h == NULL || opt == NULL || opt->input_id == NULL || opt->target_id == NULL || opt->endpoint == NULL) {
        return 0;
    }

    sw_char_array_init(&config);
    ok = sw_js_emit_live_config(&config, opt)
        && sw_js_emit_initializer(h, "live-search", "liveSearch", &config);
    sw_char_array_free(&config);
    return ok;
}

b8 (sw_js_fetch)(sw_buffer* h, const sw_js_fetch_opts* opt) {
    sw_char_array config;
    b8 ok;

    if (h == NULL || opt == NULL || opt->trigger_id == NULL || opt->target_id == NULL || opt->endpoint == NULL) {
        return 0;
    }

    sw_char_array_init(&config);
    ok = sw_js_emit_fetch_config(&config, opt)
        && sw_js_emit_initializer(h, "fetch-replace", "fetchReplace", &config);
    sw_char_array_free(&config);
    return ok;
}

b8 (sw_js_toggle)(sw_buffer* h, const sw_js_toggle_opts* opt) {
    sw_char_array config;
    b8 ok;

    if (h == NULL || opt == NULL || opt->trigger_id == NULL || opt->target_id == NULL) {
        return 0;
    }

    sw_char_array_init(&config);
    ok = sw_js_emit_toggle_config(&config, opt)
        && sw_js_emit_initializer(h, "toggle", "toggle", &config);
    sw_char_array_free(&config);
    return ok;
}

b8 (sw_js_class)(sw_buffer* h, const sw_js_class_opts* opt) {
    sw_char_array config;
    b8 ok;

    if (h == NULL || opt == NULL || opt->trigger_id == NULL || opt->target_id == NULL || opt->class_name == NULL) {
        return 0;
    }

    sw_char_array_init(&config);
    ok = sw_js_emit_class_config(&config, opt)
        && sw_js_emit_initializer(h, "class-toggle", "classToggle", &config);
    sw_char_array_free(&config);
    return ok;
}

b8 (sw_js_modal)(sw_buffer* h, const sw_js_modal_opts* opt) {
    sw_char_array config;
    b8 ok;

    if (h == NULL || opt == NULL || opt->trigger_id == NULL || opt->target_id == NULL) {
        return 0;
    }

    sw_char_array_init(&config);
    ok = sw_js_emit_modal_config(&config, opt)
        && sw_js_emit_initializer(h, "modal", "modal", &config);
    sw_char_array_free(&config);
    return ok;
}
