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

    sw_hbuf* h = sw_hbuf_new();

    sw_html(h, sw_attrs(sw_attr("lang", "en")), {
        sw_body(h, sw_no_attrs, {
            sw_h1(h, sw_no_attrs, {
                sw_text(h, "Syphax Web");
            });
        });
    });

    sw_http_reply(c, 200, "text/html; charset=utf-8", sw_hbuf_data(h), sw_hbuf_len(h));
    sw_hbuf_free(h);
}

int main(void) {
    return sw_server_listen("http://0.0.0.0:8000", handler, NULL);
}
```

Open `http://127.0.0.1:8000`.

## Common Pieces

- HTML: `sw_html`, `sw_div`, `sw_form`, `sw_input`, `sw_text`, `sw_attr`, `sw_attrs`
- JS: `sw_j_live_search`, `sw_j_live`, `sw_j_fetch`, `sw_j_toggle`, `sw_j_class`
- HTTP: `sw_http_is`, `sw_http_get_query`, `sw_http_get_form`, `sw_http_reply`, `sw_http_replyf`
- Utility: `sw_matches_query`

## Translations

Catalog file:

```json
{
  "Search": {
    "fr": "Rechercher",
    "ar": "\u0628\u062d\u062b",
    "ja": "\u691c\u7d22"
  }
}
```

Load:

```c
#include "sw_translator.h"

sw_translator* tr = sw_translator_create();
sw_translator_load_catalog_all_json_file(tr, "resources/translations.json");
sw_translator_set_language(tr, "fr");
```

English is implicit source text. No separate `en.json` needed.

Installed catalog:

- `share/syphax_web/translations.json`

## Example

`bin/syphax_web_static` serves:

- `/`
- `/search-preview`
- `/style.css`

Features in the example:

- live search
- language buttons: `en`, `fr`, `ar`, `ja`
- translated HTML

## Limits

- no TLS
- no HTTP/2
- no chunked request decoding

## License

MIT
