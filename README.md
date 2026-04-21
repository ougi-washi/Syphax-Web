# Syphax-Web

Syphax-Web is a small C web library built around two reusable pieces:

- An embeddable HTTP server API with explicit manager lifecycle.
- An HTML/translation helper for safe server-side markup generation.

The project is packaged as a library first, with examples and tests as optional targets.

## Features

- Installable `syphax_web::syphax_web` CMake target
- Linux `epoll` backend and Windows `WSAPoll` backend
- Platform-neutral public headers under `include/syphax/web`
- Safe growable HTML builder with escaping
- Explicit translator context instead of global header state
- Internal dynamic storage powered by `lib/syphax/s_array.h`

## Build

Configure a development build with examples and tests:

```bash
cmake -S . -B build \
  -DSYPHAX_WEB_BUILD_EXAMPLES=ON \
  -DSYPHAX_WEB_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Build a library-only configuration:

```bash
cmake -S . -B build
cmake --build build
```

Install the package:

```bash
cmake -S . -B build
cmake --build build
cmake --install build --prefix /your/prefix
```

## Library Usage

```cmake
find_package(syphax_web CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE syphax_web::syphax_web)
```

```c
#include "syphax/web/sw_html.h"
#include "syphax/web/sw_server.h"

static void handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    (void)request;
    (void)user_data;

    sw_html_buffer* html = sw_html_buffer_create();
    sw_html_raw(html, "<!doctype html>");
    sw_html_open_tag(html, "html", NULL);
    sw_html_open_tag(html, "body", NULL);
    sw_html_open_tag(html, "h1", NULL);
    sw_html_text(html, "Syphax Web");
    sw_html_close_tag(html, "h1");
    sw_html_close_tag(html, "body");
    sw_html_close_tag(html, "html");

    sw_http_reply(connection, 200, "text/html; charset=utf-8",
        sw_html_buffer_data(html),
        sw_html_buffer_size(html));

    sw_html_buffer_destroy(html);
}

int main(void) {
    return sw_server_listen("http://0.0.0.0:8000", handler, NULL);
}
```

## Example

With `SYPHAX_WEB_BUILD_EXAMPLES=ON`, the repository builds `syphax_web_static_example`, which serves:

- `/` with HTML generated through the builder API
- `/style.css` from `resources/style.css`

## Notes

- TLS, HTTP/2, and chunked-request decoding are not implemented in this version.
- The live socket tests need permission to open localhost sockets if your sandbox blocks networking syscalls.

## License

MIT License.
