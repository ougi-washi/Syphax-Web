# Syphax-Web

Syphax-Web is a small C web library built around three reusable pieces:

- An embeddable HTTP server API with explicit manager lifecycle.
- A short server-side HTML API for safe markup generation.
- An inline JavaScript helper layer for common DOM and request behaviors.

The project is packaged as a library first, with examples and tests as optional targets.

## Features

- Installable `syphax_web::syphax_web` CMake target
- Linux `epoll` backend and Windows `WSAPoll` backend
- Platform-neutral public headers directly under `include/`
- Safe growable HTML builder with short tag macros
- Shared inline JS runtime with reusable behavior helpers
- Explicit translator context loaded from JSON files
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
#include "sw_server.h"

static void handler(sw_connection* connection, const sw_http_message* request, void* user_data) {
    (void)user_data;

    if (!sw_http_is(request, "GET", "/")) {
        sw_http_replyf(connection, 404, "text/plain; charset=utf-8", "Not Found");
        return;
    }

    sw_hbuf* h = sw_hbuf_new();

    sw_html(h, sw_attrs(
        sw_attr("lang", "en"),
        sw_attr("data-app", "demo")
    ), {
        sw_body(h, sw_no_attrs, {
            sw_h1(h, sw_no_attrs, {
                sw_text(h, "Syphax Web");
            });
        });
    });

    sw_http_reply(connection, 200, "text/html; charset=utf-8",
        sw_hbuf_data(h),
        sw_hbuf_len(h));

    sw_hbuf_free(h);
}

int main(void) {
    return sw_server_listen("http://0.0.0.0:8000", handler, NULL);
}
```

When you bind to `0.0.0.0`, open `http://127.0.0.1:8000` or your machine's actual LAN address in the browser.

## Short HTML Pattern

- Use one small C function per page fragment instead of building one giant render function.
- Use `sw_http_is()`, `sw_http_get_query()`, and `sw_http_get_form()` to keep route and field handling readable.
- Use `sw_matches_query(text, query, case_sensitive)` when you need reusable text filtering without re-implementing substring matching in each example or route.
- Use `sw_text()` for normal escaped text output.
- Use `sw_text_no_translate()` only when you intentionally render user input, identifiers, or other literal text.
- Use `sw_attr`, `sw_attr_bool`, and `sw_attrs(...)` for most attributes.
- Use `sw_div`, `sw_form`, `sw_input`, `sw_section`, and the other tag macros for common markup.
- Use `sw_j_live_search()` when you want the common live-search pattern without spelling out every JS option.
- Use the more generic `sw_j_*` helpers when you need custom behavior.
- Keep HTTP response writing separate from HTML generation.
- Load translations explicitly from `resources/translations.json` or your own locale data through `sw_translator_load_catalog_all_json_file()`, `sw_translator_load_catalog_all_json_text()`, `sw_translator_load_catalog_json_*()`, or the lower-level flat `sw_translator_load_json_*()` helpers.

```c
#include "sw_js.h"

static void render_search_panel(sw_hbuf* h) {
    sw_section(h, sw_attrs(
        sw_attr("class", "panel"),
        sw_attr("data-component", "search-panel")
    ), {
        sw_form(h, sw_attrs(
            sw_attr("id", "search-form"),
            sw_attr("action", "/"),
            sw_attr("method", "get")
        ), {
            sw_h2(h, sw_no_attrs, {
                sw_text(h, "Search");
            });
            sw_input(h, sw_attrs(
                sw_attr("id", "search-input"),
                sw_attr("type", "text"),
                sw_attr("name", "q"),
                sw_attr("placeholder", "Search"),
                sw_attr_bool("disabled", 0)
            ));
            sw_div(h, sw_attrs(sw_attr("id", "search-preview")), {
            });
        });
    });

    sw_j_live_search(h, "search-form", "search-input", "search-preview", "/search-preview");
}
```

## Example

With `SYPHAX_WEB_BUILD_EXAMPLES=ON`, the repository builds `bin/syphax_web_static`, which serves:

- `/` with HTML generated through the short tag API, query-string driven search, and `en` / `fr` / `ar` / `ja` language buttons backed by the JSON translator
- `/style.css` directly from `resources/style.css`
- `/search-preview`, which returns an HTML fragment that the root page swaps into an inline preview div on each character input

## Translator JSON

Translations are no longer compiled into the library. The bundled example uses one catalog file with source strings as keys, so English does not need to be rewritten into a separate `en.json`:

```json
{
  "Search": {
    "fr": "Rechercher",
    "ar": "بحث",
    "ja": "検索"
  },
  "Language": {
    "fr": "Langue",
    "ar": "اللغة",
    "ja": "言語"
  }
}
```

Load and select a language explicitly:

```c
#include "sw_translator.h"

sw_translator* translator = sw_translator_create();
sw_translator_load_catalog_all_json_file(translator, "resources/translations.json");
sw_translator_set_language(translator, "fr");
```

`sw_translator_load_catalog_all_json_*()` also registers `en` automatically as the source-language fallback, so the shared catalog does not need duplicated English values.

If you already have a flat one-language object such as `{"Search":"Rechercher"}`, you can still load it through `sw_translator_load_json_file()` or `sw_translator_load_json_text()`.

Installed packages also install the checked-in catalog file under `share/syphax_web/translations.json`.

## Notes

- The short HTML and JS surface is intentionally the only public page-generation API.
- TLS, HTTP/2, and chunked-request decoding are not implemented in this version.
- The source tree depends on `lib/syphax`; if it is missing, initialize submodules before configuring.
- The live socket tests need permission to open localhost sockets if your sandbox blocks networking syscalls.

## License

MIT License.
