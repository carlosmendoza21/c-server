# c-server

> **Work in progress.** Core HTTP handling is functional; high-performance features and a test suite are actively being developed.

A from-scratch HTTP/1.1 server in C, built to explore low-level socket programming and progressively add production-grade performance techniques.

## Current State

- TCP socket setup with `SO_REUSEADDR`
- Persistent accept loop — handles one connection at a time
- Full HTTP request reading with a 5-second receive timeout
- 8 KB request size limit
- Parses the request line (method, path, HTTP version)
- Responds to `GET /` with a simple HTML page
- Proper HTTP error responses: `400`, `404`, `405`, `408`, `431`
- `SIGPIPE` ignored to prevent crashes on broken connections

## Planned

- **Concurrency** — thread pool or non-blocking I/O (epoll) to handle multiple connections simultaneously
- **Keep-alive** — HTTP persistent connections to reduce per-request overhead
- **Static file serving** — serve files from a configurable root directory
- **Routing** — method + path dispatch to handler functions
- **Performance benchmarking** — measure throughput and latency under load
- **Test suite** — automated tests covering request parsing, error handling, and response correctness

## Build

Requires CMake 3.10+ and a C11 compiler.

```sh
cmake -B build
cmake --build build
```

## Run

```sh
./build/c-server
```

The server listens on port **8080**. Visit `http://localhost:8080/` in a browser or with curl:

```sh
curl http://localhost:8080/
```

## Project Structure

```
c-server/
├── src/
│   ├── main.c        # Entry point, socket setup, signal handling
│   ├── setup.c       # create_listen_socket()
│   └── requests.c    # Request reading, parsing, and response logic
├── include/
│   ├── server.h
│   └── request.h
└── CMakeLists.txt
```
