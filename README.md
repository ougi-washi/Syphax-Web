# Syphax-Web

Small C web library.

- HTTP server
- HTML builder
- inline JS helpers
- JSON translations

Library-first. Examples and tests are optional.

## Layout

- `include/` public headers
- `src/` library code
- `lib/syphax/` vendored submodule
- `examples/` sample app
- `tests/` test suite

## Build

```bash
git submodule update --init --recursive

cmake -S . -B build \
  -DSYPHAX_WEB_BUILD_EXAMPLES=ON \
  -DSYPHAX_WEB_BUILD_TESTS=ON

cmake --build build --parallel "$(getconf _NPROCESSORS_ONLN)"
ctest --test-dir build --output-on-failure
```

Repo binaries:

- `bin/syphax_web_static`
- `bin/syphax_web_tests`

Install:

```bash
cmake --install build --prefix /your/prefix
```

## CMake

```cmake
find_package(syphax_web CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE syphax_web::syphax_web)
```

## Minimal Server

```c
#include "sw_html.h"
#include "sw_server.h"

static void handler(sw_connection* c, const sw_http_message* hm, void* user_data) {
    (void)user_data;

    if (!sw_http_is(hm, "GET", "/")) {
        sw_http_replyf(c, 404, "text/plain; charset=utf-8", "Not Found");
        return;
    }

    sw_buffer* buffer = sw_buffer_new();

    sw_html(buffer, sw_attrs(sw_attr("lang", "en")), {
        sw_body(buffer, sw_no_attrs, {
            sw_h1(buffer, sw_no_attrs, {
                sw_text(buffer, "Syphax Web");
            });
        });
    });

    sw_http_reply(c, 200, "text/html; charset=utf-8", sw_buffer_data(buffer), sw_buffer_len(buffer));
    sw_buffer_free(buffer);
}

int main(void) {
    return sw_server_listen("http://0.0.0.0:8000", handler, NULL);
}
```

Open `http://127.0.0.1:8000`.

## Common Pieces

- HTML: `sw_html`, `sw_div`, `sw_form`, `sw_input`, `sw_text`, `sw_attr`, `sw_attrs`
- JS: `sw_js_live_search`, `sw_js_live`, `sw_js_fetch`, `sw_js_toggle`, `sw_js_class`
- HTTP: `sw_http_is`, `sw_http_get_query`, `sw_http_get_form`, `sw_http_reply`, `sw_http_replyf`
- Utility: `sw_matches_query`

## Translations

Grouped file:

```json
{
  "ar": {
    "Search": "\u0628\u062d\u062b",
    "Language": "\u0627\u0644\u0644\u063a\u0629"
  },
  "ja": {
    "Search": "\u691c\u7d22",
    "Language": "\u8a00\u8a9e"
  }
}
```

Load:

```c
#include "sw_translator.h"

sw_translator* tr = sw_translator_create("resources/translations.json",
    .code = "en",
    .label = "English",
    .direction = SW_LANGUAGE_DIRECTION_LTR
);
sw_add_language(tr, .code = "ar", .label = "Arabic", .direction = SW_LANGUAGE_DIRECTION_RTL);
sw_add_language(tr, .code = "ja", .label = "Japanese", .direction = SW_LANGUAGE_DIRECTION_LTR);
sw_translator_set_language(tr, "ja");
```

One file, one object per language. English stays the source-text fallback, so no separate `en.json` is needed.

Installed translations file:

- `share/syphax_web/translations.json`

## Example

`bin/syphax_web_static` serves:

- `/`
- `/search-preview`
- `/style.css`

Features in the example:

- live search
- language buttons: `en`, `ar`, `fa`, `zh`, `ja`
- translated HTML
- vertical preview text for `zh` and `ja` via `sw_attr(sw_direction(SW_LANGUAGE_DIRECTION_TTB))`

## Limits

- no TLS
- no HTTP/2
- no chunked request decoding

## License

MIT
