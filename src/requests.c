#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>

#define MAX_REQUEST_SIZE 8192
#define READ_TIMEOUT_SECONDS 5

// Reads from clientSocket until the end of the headers ("\r\n\r\n").
// Returns the number of bytes read, or -1 if the client closed early,
// timed out, hit a read error, or sent more than cap - 1 bytes.
static ssize_t read_request(int clientSocket, char *buf, size_t cap) {
    size_t total = 0;

    while (1) {
        if (total >= cap - 1) {
            printf("Request too large\n");
            return -1;
        }

        ssize_t n = read(clientSocket, buf + total, cap - 1 - total);
        if (n == 0) {
            printf("Connection closed by client\n");
            return -1;
        }
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Client timed out\n");
            } else {
                perror("read failed");
            }
            return -1;
        }

        total += n;
        buf[total] = '\0';

        if (strstr(buf, "\r\n\r\n")) {
            return (ssize_t)total;
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

    if (read_request(clientSocket, requestBuffer, sizeof(requestBuffer)) < 0) {
        goto cleanup;
    }

    printf("Received request:\n%s\n", requestBuffer);

    if (sscanf(requestBuffer, "%15s %255s %15s", method, path, version) != 3) {
        printf("Failed to parse request\n");
        goto cleanup;
    }

    printf("Method: %s\n", method);
    printf("Path: %s\n", path);
    printf("Version: %s\n", version);

    if (strcmp(path, "/") == 0) {
        char *body = "<html><body><h1>Working!</h1></body></html>";
        send_response(clientSocket, "200 OK", "text/html", body);
    } else {
        char *body = "<html><body><h1>404 Not Found</h1></body></html>";
        send_response(clientSocket, "404 Not Found", "text/html", body);
    }

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
