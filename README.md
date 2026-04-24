# Syphax-Web

Small C web library.

- HTTP server
- optional OpenSSL-backed TLS
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
./build.sh
ctest --test-dir build --output-on-failure
```

Enable native HTTPS support with OpenSSL:

```bash
./build.sh -tls
ctest --test-dir build-tls --output-on-failure
```

Run `./build.sh --help` for build script options. The script reuses the selected build directory, so repeat runs are incremental unless `-clean` is passed.

Repo binaries when examples and tests are enabled:

- `bin/01_simple`
- `bin/02_ssl`
- `bin/03_complex_static`
- `bin/04_complex_dynamic`
- `bin/05_web_app`
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
        sw_body(buffer, sw_attrs(), {
            sw_h1(buffer, sw_attrs(), {
                sw_text(buffer, "Syphax Web");
            });
        });
    });

    sw_http_reply(c, 200, "text/html; charset=utf-8", sw_buffer_data(buffer), sw_buffer_len(buffer));
    sw_buffer_free(buffer);
}

int main(void) {
    sw_http_config config = sw_http_config_default();
    config.max_body_bytes = 256 * 1024;
    return sw_server_listen("http://0.0.0.0:8000", &config, handler, NULL);
}
```

Open `http://127.0.0.1:8000`.

## HTTPS Server

Native TLS is opt-in at build time with `SYPHAX_WEB_ENABLE_TLS=ON`.

```c
int main(void) {
    sw_http_config http = sw_http_config_default();
    sw_tls_config tls = sw_tls_config_default();

    tls.certificate_file = "/etc/ssl/example/fullchain.pem";
    tls.private_key_file = "/etc/ssl/example/privkey.pem";

    return sw_server_listen_tls("https://0.0.0.0:8443", &http, &tls, handler, NULL);
}
```

TLS listeners serve HTTP/1.1 over TLS and negotiate ALPN `http/1.1` only. They do not advertise `h2`.

## Static Files

Use the docroot-scoped helper for public assets:

```c
if (sw_http_is(hm, "GET", "/style.css")) {
    sw_http_serve_path(c, "resources", hm->uri);
    return;
}
```

`sw_http_serve_file` still exists for explicit file paths, but `sw_http_serve_path` is the safer default for request-driven asset serving.

## Server Config

Every manager starts with bounded request and timeout defaults:

- `max_header_bytes = 16 KiB`
- `max_body_bytes = 1 MiB`
- `max_header_count = 64`
- `header_timeout_ms = 5000`
- `body_timeout_ms = 15000`
- `idle_timeout_ms = 15000`

Override them by passing a `sw_http_config` to `sw_mgr_create` or `sw_server_listen`. Pass `NULL` to use the defaults as-is.

## Common Pieces

- HTML: `sw_html`, `sw_div`, `sw_form`, `sw_input`, `sw_text`, `sw_attr`, `sw_attrs`
- JS: `sw_js_live_search`, `sw_js_live`, `sw_js_fetch`, `sw_js_toggle`, `sw_js_class`
- HTTP: `sw_http_is`, `sw_http_get_query`, `sw_http_get_form`, `sw_http_reply`, `sw_http_replyf`, `sw_http_serve_path`
- Utility: `sw_matches_query`

## Translations

Grouped file:

```json
{
  "ar": {
    "Search": "بحث",
    "Language": "اللغة"
  },
  "ja": {
    "Search": "検索",
    "Language": "言語"
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

## Examples

All examples use the shared stylesheet in `examples/shared/style.css`.

- `bin/01_simple`: HTTP-only minimal server on `http://127.0.0.1:8000`
- `bin/02_ssl`: HTTPS status page on `https://127.0.0.1:8443`
- `bin/03_complex_static`: HTTPS static site with HTML, JSON, text, and CSS routes on `https://127.0.0.1:8444`
- `bin/04_complex_dynamic`: HTTPS dynamic queue using query/form helpers and inline JS helpers on `https://127.0.0.1:8445`
- `bin/05_web_app`: HTTPS folder app served from `examples/advanced` on `https://127.0.0.1:8446`

The TLS examples require a certificate and private key. Do not commit private keys to the repo. For local HTTPS, generate a localhost certificate with a local CA such as `mkcert`:

```bash
TRUST_STORES=system,nss mkcert -install
mkcert \
  -cert-file examples/shared/localhost.local.crt \
  -key-file examples/shared/localhost.local.key \
  localhost 127.0.0.1 ::1

./build.sh -tls
./bin/02_ssl
```

The examples automatically use `examples/shared/localhost.local.crt` and `examples/shared/localhost.local.key` when they exist. Those files are ignored by git. You can also point at a real certificate explicitly:

If the browser still shows a warning after `mkcert -install`, restart the browser and open `https://127.0.0.1:8443` again. Firefox may need NSS trust-store installation, which is why the command above sets `TRUST_STORES=system,nss`.

```bash
SYPHAX_WEB_TLS_CERT=/path/to/fullchain.pem \
SYPHAX_WEB_TLS_KEY=/path/to/privkey.pem \
./bin/02_ssl
```

## Scope

- request size and timeout config defaults are enforced, but this is still a small HTTP/1.1 server layer rather than a full edge proxy
- native TLS is available only when built with OpenSSL
- HTTP/2 is not implemented natively; use nginx, Caddy, HAProxy, or another edge proxy to terminate HTTP/2 and forward HTTP/1.1 to `sw_http_listen`
- no chunked request decoding

## License

MIT
