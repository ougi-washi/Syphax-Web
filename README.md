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

Initialize the embedded dependency first:

```bash
git submodule update --init --recursive
```

Configure a development build with examples and tests:

```bash
cmake -S . -B build \
  -DSYPHAX_WEB_BUILD_EXAMPLES=ON \
  -DSYPHAX_WEB_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

Development executables are written to `bin/` in the repo root:

- `bin/syphax_web_static`
- `bin/syphax_web_tests`

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

When you bind to `0.0.0.0`, open `http://127.0.0.1:8000` or your machine's actual LAN address in the browser.

## Example

With `SYPHAX_WEB_BUILD_EXAMPLES=ON`, the repository builds `bin/syphax_web_static`, which serves:

- `/` with HTML generated through the builder API
- `/style.css` from `resources/style.css`, with a built-in fallback so the example still works outside the repo root

## Notes

- TLS, HTTP/2, and chunked-request decoding are not implemented in this version.
- The source tree depends on `lib/syphax`; if it is missing, initialize submodules before configuring.
- The live socket tests need permission to open localhost sockets if your sandbox blocks networking syscalls.

## License

MIT License.
