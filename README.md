# Syphax-Web

Syphax-Web is a small C web library built around two reusable pieces:

- An embeddable HTTP server API with explicit manager lifecycle.
- An HTML/translation helper for safe server-side markup generation.
- An inline JavaScript helper layer for common DOM and request behaviors.

The project is packaged as a library first, with examples and tests as optional targets.

## Features

- Installable `syphax_web::syphax_web` CMake target
- Linux `epoll` backend and Windows `WSAPoll` backend
- Platform-neutral public headers directly under `include/`
- Safe growable HTML builder with escaping
- Shared inline JS runtime with reusable behavior helpers
- Explicit translator context instead of global header state
- Internal dynamic storage powered by the vendored `lib/syphax` submodule

## Layout

- `include/`: public library headers
- `src/`: library implementation and internal headers
- `lib/syphax/`: vendored `syphax` submodule
- `examples/`: small consumer example programs
- `tests/`: library test programs

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
cmake --build build --parallel "$(getconf _NPROCESSORS_ONLN)"
ctest --test-dir build --output-on-failure
./bin/syphax_web_tests
```

Development executables are written to `bin/` in the repo root:

- `bin/syphax_web_static`
- `bin/syphax_web_tests`

CTest only reads the configured build tree, not a test executable path.
Use `ctest --test-dir build -VV` for CTest, or run `./bin/syphax_web_tests` directly.

Build a library-only configuration:

```bash
cmake -S . -B build
cmake --build build --parallel "$(getconf _NPROCESSORS_ONLN)"
```

Install the package:

```bash
cmake -S . -B build
cmake --build build --parallel "$(getconf _NPROCESSORS_ONLN)"
cmake --install build --prefix /your/prefix
```

## Library Usage

```cmake
find_package(syphax_web CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE syphax_web::syphax_web)
```

```c
#include "sw_html.h"
#include "sw_js.h"
#include "sw_server.h"

static void handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    (void)request;
    (void)user_data;

    sw_html_buffer* html = sw_html_buffer_create();
    sw_html_open_tag(html, "html", sw_html_attr_items(
        sw_html_attr_kv("lang", "en"),
        sw_html_attr_kv("data-app", "demo")
    ));
    sw_html_open_tag(html, "body", NULL, 0);
    sw_html_open_tag(html, "h1", NULL, 0);
    sw_html_text_tr(html, "Syphax Web");
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

## Dynamic HTML Pattern

- Use one small C function per page fragment instead of building one giant render function.
- Use `sw_html_text()` for escaped text and `sw_html_text_tr()` when you want translation too.
- Use `sw_html_open_tag()` and `sw_html_void_tag()` with `sw_html_attr_items(...)` for custom `data-*`, `aria-*`, and boolean attributes.
- Use `sw_js_*()` helpers to emit common browser behavior inline without maintaining a separate asset for routine interactions.
- Keep HTTP response writing separate from HTML generation.

```c
static void render_search_panel(sw_html_buffer* html) {
    const sw_js_live_search_options live_search = {
        .form_id = "search-form",
        .input_id = "search-input",
        .target_id = "search-preview",
        .endpoint = "/search-preview",
        .value_param = "q",
        .loading_class = "is-loading",
        .debounce_ms = 120,
        .method = SW_JS_HTTP_POST,
        .swap_mode = SW_JS_SWAP_INNER_HTML,
        .serialize_form = 1,
        .abort_stale = 1,
        .prevent_submit = 1
    };

    sw_html_open_tag(html, "section", sw_html_attr_items(
        sw_html_attr_kv("class", "panel"),
        sw_html_attr_kv("data-component", "search-panel")
    ));
    sw_html_open_tag(html, "form", sw_html_attr_items(
        sw_html_attr_kv("id", "search-form"),
        sw_html_attr_kv("action", "/"),
        sw_html_attr_kv("method", "post")
    ));
    sw_html_open_tag(html, "h2", NULL, 0);
    sw_html_text_tr(html, "Search");
    sw_html_close_tag(html, "h2");
    sw_html_void_tag(html, "input", sw_html_attr_items(
        sw_html_attr_kv("id", "search-input"),
        sw_html_attr_kv("type", "text"),
        sw_html_attr_kv("name", "q"),
        sw_html_attr_kv_tr("placeholder", "Search"),
        sw_html_attr_bool("disabled", 0)
    ));
    sw_html_open_tag(html, "div", sw_html_attr_items(
        sw_html_attr_kv("id", "search-preview")
    ));
    sw_html_close_tag(html, "div");
    sw_html_close_tag(html, "form");
    sw_html_close_tag(html, "section");

    sw_js_live_search(html, &live_search);
}
```

## Example

With `SYPHAX_WEB_BUILD_EXAMPLES=ON`, the repository builds `bin/syphax_web_static`, which serves:

- `/` with HTML generated through the builder API
- `/style.css` directly from `resources/style.css`
- `/search-preview`, which returns an HTML fragment that the root page swaps into an inline preview div on each character input

## Notes

- TLS, HTTP/2, and chunked-request decoding are not implemented in this version.
- The source tree depends on `lib/syphax`; if it is missing, initialize submodules before configuring.
- The live socket tests need permission to open localhost sockets if your sandbox blocks networking syscalls.

## License

MIT License.
