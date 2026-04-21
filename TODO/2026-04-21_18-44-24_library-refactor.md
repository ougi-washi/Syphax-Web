# Syphax-Web Library Refactor TODO

## Build, Packaging, and Layout
- [x] Replace the example-first `main` target with an installable `syphax_web` library and alias target.
- [x] Make examples and tests optional CMake targets instead of default top-level outputs.
- [x] Export and install public headers plus CMake package files for library consumers on Linux and Windows.
- [x] Remove recursive source globbing and hard-coded root executable output from the build.
- [x] Update `build.sh`, `build.bat`, and `README.md` for the library-first workflow.

Verified with:
- `CMakeLists.txt`, `cmake/syphax_webConfig.cmake.in`, and the new `include/` tree
- `build.sh`, `build.bat`, and `README.md`

## Public API and Shared Utilities
- [x] Move the public API to an installable include tree under `include/syphax/web`.
- [x] Replace local fixed-array helpers with `lib/syphax/s_array.h`.
- [x] Remove global translation state from public headers and introduce an explicit translator context.
- [x] Replace the raw HTML string buffer with a growable builder API backed by `s_array`.
- [x] Escape HTML text and attribute values and emit valid void tags.

Verified with:
- public headers under `include/syphax/web`
- obsolete `src/sw_*.h` headers removed
- `src/sw_utility.c`, `src/sw_translator.c`, and `src/sw_html.c`

## Server Runtime
- [x] Make public server headers platform-neutral instead of exposing POSIX socket types.
- [x] Introduce explicit manager lifecycle and handler user-data APIs suitable for library embedding.
- [x] Keep the Linux `epoll` backend and add a Windows `WSAPoll` backend behind a shared internal interface.
- [x] Fix listener and connection event bookkeeping so handlers are not dropped and events dispatch correctly.
- [x] Fix HTTP parsing so requests are only dispatched once the full header/body payload is available.
- [x] Reject unsupported chunked requests cleanly instead of partially parsing them.
- [x] Fix reply and file-serving paths so headers/body are not duplicated and large payloads are streamed safely.

Verified with:
- `src/sw_server.c`, `src/sw_backend_epoll.c`, `src/sw_backend_wsapoll.c`, `src/sw_internal.h`
- Linux consumer builds with `cc`
- Windows cross-build with `x86_64-w64-mingw32-gcc`

## Examples and Tests
- [x] Update the example program to consume the library API and HTML builder correctly.
- [x] Add unit tests for translation, HTML generation, and request parsing helpers.
- [x] Add integration coverage for live HTTP listen/reply behavior and static file serving.
- [x] Run available compile/test verification in this environment, including warning-enabled Linux builds, an unsandboxed localhost-socket test run, and a Windows cross-build, then mark every checklist line complete only after verification.

Verified with:
- `examples/static.c`
- `tests/test_suite.c`
- `cc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Iinclude -Ilib -Isrc examples/static.c src/sw_utility.c src/sw_translator.c src/sw_html.c src/sw_server.c src/sw_backend_epoll.c -o /tmp/syphax_web_example`
- `cc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Iinclude -Ilib -Isrc tests/test_suite.c src/sw_utility.c src/sw_translator.c src/sw_html.c src/sw_server.c src/sw_backend_epoll.c -o /tmp/syphax_web_tests`
- `/tmp/syphax_web_tests` run outside the sandbox to allow localhost sockets
- `x86_64-w64-mingw32-gcc -std=c11 -Wall -Wextra -Wpedantic -Wshadow -Iinclude -Ilib -Isrc examples/static.c src/sw_utility.c src/sw_translator.c src/sw_html.c src/sw_server.c src/sw_backend_wsapoll.c -lws2_32 -o /tmp/syphax_web_example.exe`
