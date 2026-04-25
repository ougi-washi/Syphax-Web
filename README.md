# Syphax-Web

Small C web toolkit for server-rendered apps.

- HTTP/1.1 server
- optional OpenSSL TLS
- HTML builder
- small JS helpers
- JSON-backed translations
- CMake install package

## Build

```bash
./build.sh
```

TLS build:

```bash
./build.sh -tls
```

Install:

```bash
cmake --install build --prefix /your/prefix
```

## Link

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

    sw_buffer* h = sw_buffer_new();

    sw_html(h, sw_attrs(sw_attr("lang", "en")), {
        sw_body(h, sw_attrs(), {
            sw_h1(h, sw_attrs(), {
                sw_text(h, "Syphax Web");
            });
        });
    });

    sw_http_reply(c, 200, "text/html; charset=utf-8", sw_buffer_data(h), sw_buffer_len(h));
    sw_buffer_free(h);
}

int main(void) {
    return sw_server_listen("http://0.0.0.0:8000", NULL, handler, NULL);
}
```

Open `http://127.0.0.1:8000`.

Pass `NULL` for default request limits and timeouts. Use `sw_http_config_default()` when you want to override them.

## TLS

Native HTTPS is enabled with `./build.sh -tls`.

```c
int main(void) {
    sw_tls_config tls = sw_tls_config_default();

    tls.certificate_file = "/etc/ssl/example/fullchain.pem";
    tls.private_key_file = "/etc/ssl/example/privkey.pem";

    return sw_server_listen_tls("https://0.0.0.0:8443", NULL, &tls, handler, NULL);
}
```

Local example certs:

```bash
TRUST_STORES=system,nss mkcert -install
mkcert \
  -cert-file examples/shared/localhost.local.crt \
  -key-file examples/shared/localhost.local.key \
  localhost 127.0.0.1 ::1

./build.sh -tls
./bin/02_https
```

The TLS examples also accept:

```bash
SYPHAX_WEB_TLS_CERT=/path/to/fullchain.pem \
SYPHAX_WEB_TLS_KEY=/path/to/privkey.pem \
./bin/02_https
```

## Static Files

Serve public assets from a fixed docroot:

```c
if (sw_http_is(hm, "GET", "/style.css")) {
    sw_http_serve_path(c, "resources", hm->uri);
    return;
}
```

`sw_http_serve_path` keeps requests inside the docroot.

## Translations

One JSON object per source string:

```json
{
  "Search": {
    "ar": "بحث",
    "ja": "検索"
  },
  "Language": {
    "ar": "اللغة",
    "ja": "言語"
  }
}
```

```c
#include "sw_translator.h"

sw_translator* tr = sw_translator_create("resources/translations.json",
    .code = "en",
    .label = "English",
    .direction = SW_LANGUAGE_DIRECTION_LTR
);

sw_translator_add(tr, .code = "ar", .label = "Arabic", .direction = SW_LANGUAGE_DIRECTION_RTL);
sw_translator_add(tr, .code = "ja", .label = "Japanese", .direction = SW_LANGUAGE_DIRECTION_LTR);
sw_translator_set_language(tr, "ja");
```

English source text is the fallback. The installed file is `share/syphax_web/translations.json`.

## Common API

- HTML: `sw_html`, `sw_div`, `sw_form`, `sw_input`, `sw_text`, `sw_attr`, `sw_attrs`
- HTTP: `sw_http_is`, `sw_http_get_query`, `sw_http_get_form`, `sw_http_reply`, `sw_http_replyf`, `sw_http_serve_path`
- JS: `sw_js_live_search`, `sw_js_live`, `sw_js_fetch`, `sw_js_toggle`, `sw_js_class`
- Utility: `sw_matches_query`

## Examples

Build outputs:

- `bin/01_http`: HTTP app on `http://127.0.0.1:8000`
- `bin/02_https`: HTTPS status page on `https://127.0.0.1:8443`
- `bin/03_static_site`: static HTTPS site on `https://127.0.0.1:8444`
- `bin/04_live_queue`: live form demo on `https://127.0.0.1:8445`
- `bin/05_folder_app`: folder-backed app on `https://127.0.0.1:8446`

## Fit

Use Syphax-Web for embedded tools, local dashboards, internal services, and small server-rendered apps.

Use a front proxy for HTTP/2, compression, rate limits, and edge policy.

TLS listeners speak HTTP/1.1. Request bodies are bounded. Chunked request bodies are a future add.

## License

MIT
