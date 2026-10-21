#!/usr/bin/env bash
# Integration tests: start the real server, send it requests, check the responses.
# Usage (from the repo root): tests/integration.sh [path/to/c-server]
# Exits 0 if every check passes, 1 otherwise.
set -u

BIN="${1:-./build/c-server}"
PORT=8080
BASE="http://localhost:$PORT"

PASS=0
FAIL=0
SERVER_PID=""
TMP="$(mktemp -d)"
LOG="$TMP/server.log"

# Files created inside the repo for the tests; all removed on exit.
FIXTURES=(public/big.bin public/escape.md public/etc public/alias.html public/sib
          public/notes..txt public/..hidden public/locked.html public_secret)

pass() { PASS=$((PASS + 1)); echo "  PASS  $1"; }
fail() { FAIL=$((FAIL + 1)); echo "  FAIL  $1"; }

cleanup() {
    if [ -n "$SERVER_PID" ] && kill -0 "$SERVER_PID" 2>/dev/null; then
        kill "$SERVER_PID"
        wait "$SERVER_PID" 2>/dev/null
    fi
    chmod 644 public/locked.html 2>/dev/null
    rm -rf "${FIXTURES[@]}" "$TMP"
}
trap cleanup EXIT

# expect_status <code> <path> [extra curl args...]
expect_status() {
    local want="$1" path="$2"
    shift 2
    local got args="$*"
    [ ${#args} -gt 40 ] && args="${args:0:40}..."   # keep long headers out of the output
    got=$(curl -s --path-as-is --max-time 3 -o /dev/null -w '%{http_code}' "$@" "$BASE$path")
    if [ "$got" = "$want" ]; then
        pass "$want  $path $args"
    else
        fail "$path $args: expected $want, got $got"
    fi
}

# raw_status <bytes> [timeout]: send raw bytes, print the reply's status code (or "none")
raw_status() {
    local reply
    reply=$(timeout "${2:-3}" bash -c \
        'exec 3<>/dev/tcp/127.0.0.1/'"$PORT"'; printf "%b" "$1" >&3; head -n1 <&3' _ "$1" \
        2>/dev/null | tr -d '\r')
    if [ -z "$reply" ]; then echo none; else echo "$reply" | cut -d' ' -f2; fi
}

expect_raw() {   # expect_raw <code> <description> <bytes> [timeout]
    local got
    got=$(raw_status "$3" "${4:-3}")
    if [ "$got" = "$1" ]; then pass "$1  $2"; else fail "$2: expected $1, got $got"; fi
}

fd_count() { ls "/proc/$SERVER_PID/fd" | wc -l; }

# ---------- Preconditions ----------

if [ ! -d public ]; then
    echo "Run this from the repo root (public/ not found)."
    exit 1
fi
if [ ! -x "$BIN" ]; then
    echo "Server binary not found at $BIN. Build first: cmake --build build"
    exit 1
fi
if curl -s -o /dev/null --max-time 1 "$BASE/"; then
    echo "Something is already listening on port $PORT. Stop it and re-run."
    exit 1
fi
BIN_ABS="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"

# ---------- Startup ----------

echo "Startup"
mkdir "$TMP/empty"
(cd "$TMP/empty" && timeout 3 "$BIN_ABS" >/dev/null 2>&1)
code=$?
if [ "$code" = 1 ]; then pass "refuses to start without public/"; else fail "start without public/: exit code $code, expected 1"; fi

# ---------- Fixtures ----------

head -c 5000000 /dev/urandom > public/big.bin
ln -s ../README.md public/escape.md            # points outside public/
ln -s /etc public/etc                          # directory outside public/
ln -s about.html public/alias.html             # stays inside public/
mkdir -p public_secret && echo pw > public_secret/x.txt
ln -s ../public_secret public/sib              # sibling sharing the "public" prefix
echo hi > public/notes..txt
echo hi > public/..hidden
echo secret > public/locked.html && chmod 000 public/locked.html

# ---------- Start the server ----------

"$BIN" > "$LOG" 2>&1 &
SERVER_PID=$!
for _ in $(seq 50); do
    curl -s -o /dev/null "$BASE/" && break
    sleep 0.1
done
if ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "Server failed to start. Log:"
    cat "$LOG"
    exit 1
fi
FDS_START=$(fd_count)

# ---------- Static files ----------

echo "Static files"
expect_status 200 /
expect_status 200 /index.html
expect_status 200 /about.html
expect_status 200 /css/style.css
expect_status 200 "/index.html?v=2"
expect_status 200 /notes..txt
expect_status 200 /..hidden
expect_status 200 /alias.html
expect_status 404 /nope.html
expect_status 404 /css
expect_status 404 /index.html/x

got_len=$(curl -s -D - -o /dev/null "$BASE/" | tr -d '\r' | awk 'tolower($1) == "content-length:" {print $2}')
want_len=$(wc -c < public/index.html)
if [ "$got_len" = "$want_len" ]; then pass "Content-Length matches file size ($want_len)"; else fail "Content-Length: got $got_len, file is $want_len bytes"; fi

if curl -s "$BASE/big.bin" -o "$TMP/big.out" && cmp -s public/big.bin "$TMP/big.out"; then
    pass "5 MB file is byte-for-byte identical"
else
    fail "5 MB file differs after download"
fi

# ---------- Path traversal ----------

echo "Path traversal"
expect_status 403 /../README.md
expect_status 403 /css/../../README.md
expect_status 403 /css/..
expect_status 403 "/../README.md?x=1"
expect_status 403 /../../../etc/passwd
expect_status 403 /escape.md
expect_status 403 /etc/passwd
expect_status 403 /sib/x.txt
expect_raw 400 "relative path (GET ../README.md)" 'GET ../README.md HTTP/1.1\r\n\r\n'
if [ "$(id -u)" != 0 ]; then          # root can read mode-000 files
    expect_status 403 /locked.html
fi

# ---------- Request handling ----------

echo "Request handling"
expect_status 405 / -X POST
expect_status 405 / -X DELETE
expect_raw 400 "malformed request line" 'garbage\r\n\r\n'
expect_status 431 / -H "X-Big: $(head -c 10000 /dev/zero | tr '\0' a)"

split=$(timeout 5 bash -c 'exec 3<>/dev/tcp/127.0.0.1/'"$PORT"'
    printf "GET / HTTP/1.1\r\n" >&3; sleep 1; printf "Host: x\r\n\r\n" >&3
    head -n1 <&3' 2>/dev/null | tr -d '\r' | cut -d' ' -f2)
if [ "$split" = 200 ]; then pass "200  request split across two writes"; else fail "split request: expected 200, got ${split:-none}"; fi

expect_raw 408 "silent client times out" '' 8

timeout 1 bash -c 'exec 3<>/dev/tcp/127.0.0.1/'"$PORT" 2>/dev/null   # connect and leave
expect_status 200 / ; echo "        (server still serving after an empty connection)"

# ---------- Resource checks ----------

echo "Resources"
for _ in $(seq 20); do
    curl -s -o /dev/null "$BASE/"
    curl -s -o /dev/null "$BASE/nope.html"
    curl -s -o /dev/null "$BASE/css"
    curl -s -o /dev/null --path-as-is "$BASE/../README.md"
    curl -s -o /dev/null "$BASE/escape.md"
done
FDS_END=$(fd_count)
if [ "$FDS_END" = "$FDS_START" ]; then pass "no file descriptor leaks ($FDS_START open before and after)"; else fail "fd leak: $FDS_START open before, $FDS_END after"; fi

if kill -0 "$SERVER_PID" 2>/dev/null; then pass "server still running"; else fail "server crashed"; fi
if grep -q AddressSanitizer "$LOG"; then
    fail "AddressSanitizer reported an error:"
    grep -A20 AddressSanitizer "$LOG"
else
    pass "no AddressSanitizer errors"
fi

# ---------- Summary ----------

echo
echo "$PASS passed, $FAIL failed"
[ "$FAIL" = 0 ]
