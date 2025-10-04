// Syphax-Web - Ougi Washi

#include "sw_server.h"
#include "sw_html.h"

void http_handler(sw_connection_t *c, sw_http_message_t *hm) {

    if (strcmp(hm->method, "GET") == 0) {
        if (strcmp(hm->uri, "/") == 0) {
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
            sw_http_reply(c, 200, "", content);
            sw_destroy_html_buffer(content);
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
    sw_mgr_t mgr = {0};
    if (sw_mgr_init(&mgr) != 0) {
        fprintf(stderr, "Failed to initialize manager\n");
        return 1;
    }
    
    sw_mgr_set_http_handler(&mgr, http_handler);
    
    if (sw_http_listen(&mgr, "http://0.0.0.0:8080") != 0) {
        fprintf(stderr, "Failed to listen on port 8080\n");
        return 1;
    }
    
    printf("Syphax Web Server running on http://localhost:8080\n");
    printf("\nPress Ctrl+C to stop\n");
    
    while (1) {
        sw_mgr_poll(&mgr, 1000);
    }
    
    sw_mgr_free(&mgr);
}
