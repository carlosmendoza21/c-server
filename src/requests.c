#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include "request.h"

#define MAX_REQUEST_SIZE 8192
#define READ_TIMEOUT_SECONDS 5
#define DOC_ROOT "public"
#define FILE_CHUNK_SIZE 8192

enum read_result {
    READ_SUCCESS,
    READ_CLOSED,
    READ_TIMEOUT,
    READ_ERROR,
    READ_TOO_LARGE
};

enum path_result {
    PATH_SUCCESS,
    PATH_TOO_LONG,
    PATH_BAD_REQUEST,
    PATH_FORBIDDEN
};

// Absolute, symlink-free path of DOC_ROOT, set once by init_doc_root().
static char resolved_root[PATH_MAX];
static size_t resolved_root_len;

// Resolves DOC_ROOT to an absolute path. Call once at startup.
// Returns 0 on success, -1 if DOC_ROOT doesn't exist.
int init_doc_root(void) {
    if (realpath(DOC_ROOT, resolved_root) == NULL) {
        perror("realpath(DOC_ROOT) failed");
        return -1;
    }
    resolved_root_len = strlen(resolved_root);
    printf("Serving files from %s\n", resolved_root);
    return 0;
}

// Reads an HTTP request from the clientSocket into buf, ensuring it does not exceed cap bytes.
// Returns an enum indicating the result of the read operation.

static enum read_result read_request(int clientSocket, char *buf, size_t cap) {
    size_t total = 0;

    while (1) {
        if (total >= cap - 1) {
            printf("Request too large\n");
            return READ_TOO_LARGE;
        }

        ssize_t n = read(clientSocket, buf + total, cap - 1 - total);
        if (n == 0) {
            printf("Connection closed by client\n");
            return READ_CLOSED;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Client timed out\n");
                return READ_TIMEOUT;
            } else {
                perror("read failed");
                return READ_ERROR;  
            }
        }

        total += n;
        buf[total] = '\0';

        if (strstr(buf, "\r\n\r\n")) {
            return READ_SUCCESS;
        }
    }
}

// Sends the entire buffer to the clientSocket, handling partial sends and interruptions.
static int send_all(int fd, const char *buf, size_t len){
    size_t total_sent = 0;

    while (total_sent < len) {
        ssize_t sent = send(fd, buf + total_sent, len - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("send failed");
            return -1;
        }
        total_sent += sent;
    }
    return 0;
}

// Sends a complete HTTP response to the clientSocket with the given status, content type, and body.
static void send_response(int clientSocket, const char *status, const char *contentType, const char *body) {
    char response[1024];
    int len = snprintf(response, sizeof(response),
             "HTTP/1.1 %s\r\n"
             "Content-Type: %s\r\n"
             "Content-Length: %zu\r\n"
             "Connection: close\r\n"
             "\r\n"
             "%s",
             status, contentType, strlen(body), body);

    if (len < 0 || (size_t)len >= sizeof(response)) {
        printf("Response too large to send\n");
        return;
    }
    send_all(clientSocket, response, len);
}

// Returns 1 if any '/'-separated segment of path is exactly "..", else 0.
static int has_dot_dot_segment(const char *path) {
    const char *start = path;

    while (*path) {
        int at_segment_start = (path == start || path[-1] == '/');
        if (at_segment_start &&
            path[0] == '.' && path[1] == '.' &&
            (path[2] == '/' || path[2] == '\0')) {
            return 1;
        }
        path++;
    }
    return 0;
}

// Converts a URL path to a file system path by prepending the document root and handling query parameters and default index files.
// Returns PATH_SUCCESS, or the reason the path was rejected.
static enum path_result build_file_path(const char *requestPath, char *filePath, size_t filePathSize) {
    if (requestPath[0] != '/') {
        printf("Request path does not start with '/'\n");
        return PATH_BAD_REQUEST;
    }

    int len = snprintf(filePath, filePathSize, "%s%s", DOC_ROOT, requestPath);
    if (len < 0 || (size_t)len >= filePathSize) {
        printf("File path too long\n");
        return PATH_TOO_LONG;
    }
    size_t length_of_file_path = strcspn(filePath, "?");
    filePath[length_of_file_path] = '\0';

    if (has_dot_dot_segment(filePath)) {
        printf("Path traversal attempt: %s\n", requestPath);
        return PATH_FORBIDDEN;
    }

    // If the path ends with a slash, append index.html
    if (filePath[length_of_file_path - 1] == '/') {
        if (length_of_file_path + strlen("index.html") >= filePathSize) {
            printf("File path too long for index.html\n");
            return PATH_TOO_LONG;
        }
        strcat(filePath, "index.html");
    }
    return PATH_SUCCESS;
}

// Returns 1 if resolved is resolved_root itself or a path inside it, else 0.
// The character after the prefix must be '/' or '\0', so a sibling
// directory like ".../public_secret" doesn't count as inside ".../public".
static int is_inside_root(const char *resolved) {
    if (strncmp(resolved, resolved_root, resolved_root_len) != 0) {
        return 0;
    }
    char next = resolved[resolved_root_len];
    return next == '/' || next == '\0';
}

// Serves the file at DOC_ROOT + urlPath, or an error response if it can't.
// Does not close clientSocket; handle_requests does that.
static void serve_file(int clientSocket, const char *urlPath) {
    char filePath[512];
    char resolvedPath[PATH_MAX];
    char header[512];
    char chunk[FILE_CHUNK_SIZE];
    struct stat st;

    switch (build_file_path(urlPath, filePath, sizeof(filePath))) {
        case PATH_SUCCESS:
            break;
        case PATH_TOO_LONG:
            send_response(clientSocket, "414 URI Too Long", "text/plain", "Request path too long");
            return;
        case PATH_BAD_REQUEST:
            send_response(clientSocket, "400 Bad Request", "text/plain", "Bad request path");
            return;
        case PATH_FORBIDDEN:
            send_response(clientSocket, "403 Forbidden", "text/plain", "Forbidden");
            return;
    }

    // Resolve symlinks, "." and ".." to the path the kernel will actually open,
    // then make sure it's still inside the document root.
    if (realpath(filePath, resolvedPath) == NULL) {
        if (errno == ENOENT || errno == ENOTDIR) {
            send_response(clientSocket, "404 Not Found", "text/plain", "File not found");
        } else if (errno == EACCES) {
            send_response(clientSocket, "403 Forbidden", "text/plain", "Forbidden");
        } else {
            perror("realpath failed");
            send_response(clientSocket, "500 Internal Server Error", "text/plain", "Failed to resolve path");
        }
        return;
    }

    if (!is_inside_root(resolvedPath)) {
        printf("Path escapes document root: %s -> %s\n", urlPath, resolvedPath);
        send_response(clientSocket, "403 Forbidden", "text/plain", "Forbidden");
        return;
    }

    // Open the path we just checked, not the original one.
    int file_fd = open(resolvedPath, O_RDONLY);
    if (file_fd < 0) {
        if (errno == ENOENT) {
            send_response(clientSocket, "404 Not Found", "text/plain", "File not found");
        } else if (errno == EACCES) {
            send_response(clientSocket, "403 Forbidden", "text/plain", "Forbidden");
        } else {
            perror("open failed");
            send_response(clientSocket, "500 Internal Server Error", "text/plain", "Failed to open file");
        }
        return;
    }

    // From here on file_fd is open, so every exit goes through close_file.

    // fstat the open file (not the path) so we check the same file we send.
    if (fstat(file_fd, &st) < 0) {
        perror("fstat failed");
        send_response(clientSocket, "500 Internal Server Error", "text/plain", "Failed to read file info");
        goto close_file;
    }
    if (!S_ISREG(st.st_mode)) {
        send_response(clientSocket, "404 Not Found", "text/plain", "File not found");
        goto close_file;
    }

    // TODO: pick Content-Type from the file extension (MIME types step)
    int len = snprintf(header, sizeof(header),
             "HTTP/1.1 200 OK\r\n"
             "Content-Type: text/html\r\n"
             "Content-Length: %lld\r\n"
             "Connection: close\r\n"
             "\r\n",
             (long long)st.st_size);
    if (len < 0 || (size_t)len >= sizeof(header)) {
        printf("Response header too large\n");
        goto close_file;
    }
    if (send_all(clientSocket, header, len) < 0) {
        goto close_file;
    }

    // Headers are already sent, so errors from here on can only stop the transfer.
    while (1) {
        ssize_t n = read(file_fd, chunk, sizeof(chunk));
        if (n == 0) {
            break;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("read file failed");
            break;
        }
        if (send_all(clientSocket, chunk, n) < 0) {
            break;
        }
    }

close_file:
    close(file_fd);
}

static void handle_requests(int clientSocket) {

    char requestBuffer[MAX_REQUEST_SIZE];

    char method[16];
    char path[256];
    char version[16];

    struct timeval timeout = { .tv_sec = READ_TIMEOUT_SECONDS, .tv_usec = 0 };
    if (setsockopt(clientSocket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        perror("setsockopt SO_RCVTIMEO failed");
        goto cleanup;
    }

    enum read_result result = read_request(clientSocket, requestBuffer, sizeof(requestBuffer));
    
    switch (result) {
        case READ_SUCCESS:
            break;
        case READ_CLOSED:
        case READ_ERROR:
            goto cleanup;
        case READ_TIMEOUT:
            send_response(clientSocket, "408 Request Timeout", "text/plain", "Request timed out");
            goto cleanup;
        case READ_TOO_LARGE:
            send_response(clientSocket, "431 Request Header Fields Too Large", "text/plain", "Request too large");
            goto cleanup;
    }

    printf("Received request:\n%s\n", requestBuffer);

    if (sscanf(requestBuffer, "%15s %255s %15s", method, path, version) != 3) {
        printf("Failed to parse request\n");
        send_response(clientSocket, "400 Bad Request", "text/plain", "Malformed request line");
        goto cleanup;
    }

    printf("Method: %s\n", method);
    printf("Path: %s\n", path);
    printf("Version: %s\n", version);

    if (strcmp(method, "GET") != 0) {
        printf("Unsupported method: %s\n", method);
        send_response(clientSocket, "405 Method Not Allowed", "text/plain", "Only GET method is supported");
        //TODO: Add Allow header
        goto cleanup;
    }

    serve_file(clientSocket, path);

cleanup:
    close(clientSocket);
}


void perma_serve(int listening_fd){
    while (1) {
        int client_fd = accept(listening_fd, NULL, NULL);
        if(client_fd < 0) {
            perror("accept failed");
            continue;
        }
        handle_requests(client_fd);

    }
}
