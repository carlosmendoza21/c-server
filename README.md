# c-server

> **Work in progress.** The server correctly serves static files and rejects malformed and malicious requests, with an automated test suite covering both. Concurrency and performance work are next.

A from-scratch HTTP/1.1 static file server in C, built to explore low-level socket programming on Linux and progressively add production-grade performance and security techniques.

## Features

### Request handling
- Persistent accept loop (currently one connection at a time)
- Reads the full request across multiple `read()` calls until the end of the headers (`\r\n\r\n`), so requests split across TCP segments are handled
- 8 KB request header limit and a 5-second receive timeout (`SO_RCVTIMEO`), so oversized or silent clients can't exhaust memory or hang the server
- `send_all()` loops until every byte is written, since `send()` may write partially
- `SIGPIPE` ignored so a client disconnecting mid-response can't crash the server

### Static file serving
- Serves files from `public/` (e.g. `/css/style.css` → `public/css/style.css`)
- `/` and any path ending in `/` map to `index.html`; query strings are ignored
- Files are streamed in 8 KB chunks, so file size isn't limited by buffer size
- `Content-Type` is chosen from the file extension: HTML, CSS, JavaScript, JSON, plain text, images (PNG, JPEG, GIF, SVG, WebP, ICO), PDF, ZIP/TAR, MP3 and MP4
  - Text types include `charset=utf-8`; unknown extensions get `application/octet-stream`, so the browser downloads them instead of guessing
  - Matching is case-insensitive, uses only the last extension (`archive.tar.gz` → `.gz`), and ignores dots in directory names
  - The type comes from the resolved path, so a symlink is typed by its target
- The file is opened first and then checked with `fstat()` on the descriptor (not `stat()` on the path), which avoids a time-of-check/time-of-use race; directories and other non-regular files are rejected

### Path traversal protection (defense in depth)
Every request path passes three independent checks before a file is opened:

| Layer | Rejects | Example | Response |
|---|---|---|---|
| 1. Leading `/` | Relative request targets | `GET ../README.md` | `400` |
| 2. `..` segments | Any path segment that is exactly `..` (names like `notes..txt` are still allowed) | `GET /css/../../README.md` | `403` |
| 3. `realpath()` containment | Anything whose resolved path is outside the document root, including symlinks | `public/escape.md → ../README.md` | `403` |

The containment check requires the character after the root prefix to be `/` or end-of-string, so a sibling directory such as `public_secret/` isn't mistaken for being inside `public/`. The document root is resolved once at startup, and the server refuses to start if it can't be found.

### HTTP responses
| Status | When |
|---|---|
| `200 OK` | File found and sent |
| `400 Bad Request` | Malformed request line, or path doesn't start with `/` |
| `403 Forbidden` | Path traversal attempt, symlink escape, or unreadable file |
| `404 Not Found` | File doesn't exist, or the path is a directory |
| `405 Method Not Allowed` | Any method other than `GET` |
| `408 Request Timeout` | Client sent nothing for 5 seconds |
| `414 URI Too Long` | Resolved file path doesn't fit the path buffer |
| `431 Request Header Fields Too Large` | Request headers exceed 8 KB |
| `500 Internal Server Error` | Unexpected filesystem error |

If the client disconnects or the read fails, the connection is closed without a response.

## Build

Requires CMake 3.10+ and a C11 compiler on Linux.

```sh
cmake -B build
cmake --build build
```

The default build type is **Debug**, which enables `-Wall -Wextra` and AddressSanitizer. For an optimized build:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Run

Run from the repo root, because the document root `public/` is resolved relative to the current directory:

```sh
./build/c-server
```

The server listens on port **8080**. Open `http://localhost:8080/` in a browser, or try it with curl:

```sh
curl -i http://localhost:8080/                              # 200, public/index.html
curl -i http://localhost:8080/nope.html                     # 404
curl -i -X POST http://localhost:8080/                      # 405
curl -i --path-as-is http://localhost:8080/../README.md     # 403, traversal blocked
```

## Testing

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

CTest runs two suites, both built with AddressSanitizer:

**Unit tests** ([`tests/test_paths.c`](tests/test_paths.c), 55 checks): call the path-handling functions directly, with no server running. `test_paths.c` includes `requests.c` so it can reach its `static` functions.
- `has_dot_dot_segment`: whole-segment `..` detection, including names like `notes..txt` and `..hidden` that must be allowed
- `build_file_path`: URL-to-file mapping, query stripping, `index.html` for trailing `/`, rejection of relative and `..` paths, and buffer-size limits
- `is_inside_root`: containment, including sibling directories such as `public_secret/` that share the root's prefix
- `get_mime_type`: table lookups plus the edge cases: uppercase extensions, `archive.tar.gz`, no extension, a dot in a directory name, and dotfiles

**Integration tests** ([`tests/integration.sh`](tests/integration.sh), 40 checks): start the real server and send it requests with `curl` and raw TCP.
- Startup: the server refuses to start without `public/`
- Static files: status codes, `Content-Length`, and a 5 MB file compared byte-for-byte
- Content types: HTML, CSS and unknown files, a symlink without an extension that points to a CSS file, and error responses staying `text/plain`
- Path traversal: every attack from the table above, including symlinks to files, to directories, and to a sibling of `public/`
- Request handling: `405`, `400`, `431`, a request split across two writes, the `408` timeout, and empty connections
- Resources: no file descriptor leaks after 100 mixed requests (counted via `/proc/<pid>/fd`), the server is still running, and no AddressSanitizer reports

The integration script creates its test files and symlinks inside `public/` and removes them on exit, even if a check fails. It can also be run on its own from the repo root:

```sh
tests/integration.sh
```

Both suites exit non-zero on any failure. They were also checked against deliberately broken builds: removing the sibling-directory check or the `close()` on the directory path makes them fail.

## Project Structure

```
c-server/
├── src/
│   ├── main.c        # Entry point: resolves document root, opens socket, ignores SIGPIPE
│   ├── setup.c       # create_listen_socket()
│   └── requests.c    # Request reading, path validation, file serving, responses
├── include/
│   ├── server.h
│   └── request.h
├── public/           # Document root (sample site)
├── tests/
│   ├── test_paths.c      # Unit tests for path handling
│   └── integration.sh    # End-to-end tests against the running server
└── CMakeLists.txt
```

## Known Limitations

- **One connection at a time.** A slow client delays everyone else until its 5-second timeout expires. Fixing this is the next major phase.
- **No keep-alive.** Every response is sent with `Connection: close`.
- **Only `GET` is supported.** `HEAD` returns `405`, and the `405` response doesn't yet include an `Allow` header.
- **No percent-decoding of URLs.** Paths like `%20` aren't decoded yet. When decoding is added, it must happen before the traversal checks.
- **`realpath()`/`open()` race.** Someone with write access to `public/` could swap a file for a symlink between the check and the open. The planned fix is `openat2()` with `RESOLVE_BENEATH`, which makes the kernel enforce the boundary during `open` itself.

## Roadmap

**Phase 1: Correctness**
- [x] Accept loop with error handling
- [x] Full request reading with size limit and timeout
- [x] HTTP error responses
- [x] Static file serving
- [x] Path traversal protection
- [x] MIME types
- [ ] Zero-copy file sending with `sendfile()`
- [ ] Keep-alive, `HEAD`, `Allow` header

**Phase 2: Concurrency**
- [ ] Thread-per-connection baseline
- [ ] Non-blocking I/O with edge-triggered `epoll`
- [ ] Multi-core workers with `SO_REUSEPORT`
- [ ] Idle-connection timers

**Phase 3: Performance**
- [ ] Profiling with `perf` and flame graphs
- [ ] Buffer pools, `writev`, socket tuning
- [ ] `openat2()` with `RESOLVE_BENEATH`

**Phase 4: Proof**
- [ ] Benchmarks with `wrk` against nginx
- [x] Unit tests (CTest) and integration tests for path handling and request handling
- [ ] Unit tests for request parsing (once the full HTTP parser exists)
- [ ] Fuzzing the request parser with libFuzzer
- [ ] CI with sanitizers on every push
