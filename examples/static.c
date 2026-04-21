// Syphax-Web - Ougi Washi

#include "sw_server.h"
#include "sw_html.h"

void render_root(sw_connection *c) {
    c8* content = sw_init_html_buffer();
    sw_header(content);
    sw_html(content,
        sw_head(content,
            sw_title(content, "Syphax-Web");
        );
        sw_body(content, attr(.id = "body", .class = "body"),
            sw_div(content, attr(.id = "content", .class = "content"),
                sw_h1(content, attr(), sw_append(content, "Syphax-Web"));
                sw_button(content, attr(.type = "button", .class = "btn btn-primary", .id = "button"), sw_append(content, "Click me!"));
                sw_script(content, attr(), "document.getElementById('button').onclick = function() { alert('Hello World!'); }");
                sw_img(content, attr(.src = "resources/syphax-web.png", .hidden = true));
            );
        );
    );
    sw_http_reply(c, 200, "", content);
    printf("Replied with %s\n", content);
    sw_destroy_html_buffer(content);
}

void http_handler(sw_connection *c, sw_http_message *hm) {
    printf("Handling request for %s\n", hm->uri);
    if (strcmp(hm->method, "GET") == 0) {
        //if (strstr(hm->uri, "/resources/")) {
        //    printf("Serving file %s\n", hm->uri);
        //    sw_http_serve_file(c, hm->uri);
        //}
        if (strcmp(hm->uri, "/") == 0) {
            render_root(c);
        }
        else {
            sw_http_reply(c, 404, "Content-Type: text/plain\r\n", "Not Found");
        }
    }
    else {
        sw_http_reply(c, 405, "Content-Type: text/plain\r\n", "Method Not Allowed");
    }
}

i32 main(i32 argc, c8** argv) {
    sw_server_init(http_handler);
    sw_server_listen("http://0.0.0.0:8000"); // This contains a loop until the server is stopped
    sw_server_clear();
}
