// Syphax-Web - Ougi Washi

#include "sw_html.h"

i32 main(i32 argc, c8** argv) {
    c8* content = sw_init_html_buffer();
    sw_html(content,
        sw_head(content,
            sw_title(content, "Syphax-Web");
        );
        sw_body(content, attr(.id = "body", .class = "body"),
            sw_div(content, attr(.id = "content", .class = "content"),
                sw_h1(content, attr(), sw_append(content, "Syphax-Web"));
                sw_button(content, attr(.type = "button", .class = "btn btn-primary", .id = "button"), sw_append(content, "Click me!"));
            );
        );
    );

    printf("%s", content);
    sw_destroy_html_buffer(content);
}
