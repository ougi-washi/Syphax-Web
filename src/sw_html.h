// Syphax-Web - Ougi Washi

#ifndef SW_HTML_BUILDER_H
#define SW_HTML_BUILDER_H

#include "sw_types.h"
#include "sw_translator.h"
#include "sw_utility.h"
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#define SW_HTML_BUFFER_SIZE 1024 * 1024 * 2 // 2MB

static c8* sw_init_html_buffer() {
    c8* output = (c8*)malloc(SW_HTML_BUFFER_SIZE);
    output[0] = '\0';
    return output;
}

static void sw_destroy_html_buffer(c8* output) {
    free(output);
}

typedef struct {
    c8* id;
    c8* class;
    c8* name;
    c8* rel;
    c8* placeholder;
    c8* type;
    c8* value;
    c8* enctype;
    b8 checked;
    c8* label_for;
    c8* method;
    c8* action;
    c8* rows;
    c8* cols;
    c8* href;
    c8* target;
    c8* src;
    c8* onclick;
    c8* width;
    c8* height;
    c8* frameborder;
    b8 controls;
    b8 hidden;
} sw_html_tag_attributes;

static void sw_open_tag(c8* output, const c8* tag, sw_html_tag_attributes* attrs) {
    strcat(output, "<");
    strcat(output, tag);
    if (attrs->id && attrs->id[0]) {
        strcat(output, " id=\"");
        strcat(output, attrs->id);
        strcat(output, "\"");
    }
    if (attrs->class && attrs->class[0]) {
        strcat(output, " class=\"");
        strcat(output, attrs->class);
        strcat(output, "\"");
    }
    if (attrs->name && attrs->name[0]) {
        strcat(output, " name=\"");
        strcat(output, attrs->name);
        strcat(output, "\"");
    }
    if (attrs->rel && attrs->rel[0]) {
        strcat(output, " rel=\"");
        strcat(output, attrs->rel);
        strcat(output, "\"");
    }
    if (attrs->placeholder && attrs->placeholder[0]) {
        strcat(output, " placeholder=\"");
        strcat(output, sw_translate(attrs->placeholder));
        strcat(output, "\"");
    }
    if (attrs->type && attrs->type[0]) {
        strcat(output, " type=\"");
        strcat(output, attrs->type);
        strcat(output, "\"");
    }
    if (attrs->value && attrs->value[0]) {
        strcat(output, " value=\"");
        strcat(output, sw_translate(attrs->value));
        strcat(output, "\"");
    }
    if (attrs->enctype && attrs->enctype[0]) {
        strcat(output, " enctype=\"");
        strcat(output, attrs->enctype);
        strcat(output, "\"");
    }
    if (attrs->checked) {
        strcat(output, " checked");
    }
    if (attrs->label_for && attrs->label_for[0]) {
        strcat(output, " for=\"");
        strcat(output, attrs->label_for);
        strcat(output, "\"");
    }
    if (attrs->method && attrs->method[0]) {
        strcat(output, " method=\"");
        strcat(output, attrs->method);
        strcat(output, "\"");
    }
    if (attrs->action && attrs->action[0]) {
        strcat(output, " action=\"");
        strcat(output, attrs->action);
        strcat(output, "\"");
    }
    if (attrs->rows && attrs->rows[0]) {
        strcat(output, " rows=\"");
        strcat(output, attrs->rows);
        strcat(output, "\"");
    }
    if (attrs->cols && attrs->cols[0]) {
        strcat(output, " cols=\"");
        strcat(output, attrs->cols);
        strcat(output, "\"");
    }
    if (attrs->href && attrs->href[0]) {
        strcat(output, " href=\"");
        strcat(output, attrs->href);
        strcat(output, "\"");
    }
    if (attrs->target && attrs->target[0]) {
        strcat(output, " target=\"");
        strcat(output, attrs->target);
        strcat(output, "\"");
    }
    if (attrs->src && attrs->src[0]) {
        strcat(output, " src=\"");
        strcat(output, attrs->src);
        strcat(output, "\"");
    }
    if (attrs->onclick && attrs->onclick[0]) {
        strcat(output, " onclick=\"");
        strcat(output, attrs->onclick);
        strcat(output, "\"");
    }
    if (attrs->width && attrs->width[0]) {
        strcat(output, " width=\"");
        strcat(output, attrs->width);
        strcat(output, "\"");
    }
    if (attrs->height && attrs->height[0]) {
        strcat(output, " height=\"");
        strcat(output, attrs->height);
        strcat(output, "\"");
    }
    if (attrs->frameborder && attrs->frameborder[0]) {
        strcat(output, " frameborder=\"");
        strcat(output, attrs->frameborder);
        strcat(output, "\"");
    }
    if (attrs->hidden) {
        strcat(output, " hidden");
    }
    if (attrs->controls) {
        strcat(output, " controls");
    }
    strcat(output, ">");
}

static void sw_close_tag(c8* output, const c8* tag) {
    strcat(output, "</");
    strcat(output, tag);
    strcat(output, ">");
}


#define sw_text(output, str, ...) do { \
    sw_append(output, sw_translate(str), ##__VA_ARGS__); \
} while(0)

#define attr(...) ((sw_html_tag_attributes){ __VA_ARGS__ })
#define sw_tag(output, tag, attrs, content) do { \
    sw_html_tag_attributes _attrs = attrs; \
    sw_open_tag(output, tag, &_attrs); \
    content; \
    sw_close_tag(output, tag); \
} while(0);

// HTML structure macros
#define sw_html(output, content) do { \
    sw_append(output, "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n\r\n\r\n"); \
    sw_tag(output, "html", {0},content); \
} while(0);

#define sw_head(output, content) do { \
    sw_tag(output, "head", {0}, content); \
    sw_append(output, "<meta charset=\"utf-8\">"); \
} while(0);

#define sw_title(output, text) do { \
    sw_tag(output, "title", {0}, sw_append(output, text)); \
} while(0);

#define sw_link(output, attrs) do { \
    sw_tag(output, "link", attrs, {}); \
} while(0);

#define sw_body(output, attrs, content) do { \
    sw_tag(output, "body", attrs, content); \
} while(0);

#define sw_div(output, attrs, content) do { \
    sw_tag(output, "div", attrs, content); \
} while(0);

#define sw_a(output, attrs, content) do { \
    sw_tag(output, "a", attrs, content); \
} while(0);

#define sw_img(output, attrs) do { \
    sw_tag(output, "img", attrs, {}); \
} while(0);

#define sw_video(output, attrs) do { \
    sw_tag(output, "video", attrs, {}); \
} while(0);

#define sw_nav(output, attrs, content) do { \
    sw_tag(output, "nav", attrs, content); \
} while(0);

#define sw_h1(output, attrs, content) do { \
    sw_tag(output, "h1", attrs, content); \
} while(0);

#define sw_h2(output, attrs, content) do { \
    sw_tag(output, "h2", attrs, content); \
} while(0);

#define sw_h3(output, attrs, content) do { \
    sw_tag(output, "h3", attrs, content); \
} while(0);

#define sw_button(output, attrs, content) do { \
    sw_tag(output, "button", attrs, content); \
} while(0);

#define sw_form(output, attrs, content) do { \
    sw_tag(output, "form", attrs, content); \
} while(0);

#define sw_label(output, attrs, content) do { \
    sw_tag(output, "label", attrs, content); \
} while(0);

#define sw_input(output, attrs) do { \
    sw_tag(output, "input", attrs, {}); \
} while(0);

#define sw_select(output, attrs, content) do { \
    sw_tag(output, "select", attrs, content); \
} while(0);

#define sw_option(output, attrs, content) do { \
    sw_tag(output, "option", attrs, content); \
} while(0);

#define sw_textarea(output, attrs, content) do { \
    sw_tag(output, "textarea", attrs, content); \
} while(0);

#define sw_iframe(output, attrs) do { \
    sw_tag(output, "iframe", attrs, {}); \
} while(0);

#define sw_ul(output, attrs, content) do { \
    sw_tag(output, "ul", attrs, content); \
} while(0);

#define sw_ol(output, attrs, content) do { \
    sw_tag(output, "ol", attrs, content); \
} while(0);

#define sw_li(output, attrs, content) do { \
    sw_tag(output, "li", attrs, content); \
} while(0);

#define sw_br(output) do { \
    sw_tag(output, "br", {0}, {}); \
} while(0);

#define sw_script(output, attrs, text) do { \
    sw_tag(output, "script", attrs, sw_append(output, text)); \
} while(0);

#endif // SW_HTML_BUILDER_H
