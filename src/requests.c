#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>

int handle_requests(int fd) {

    char requestBuffer[1024];


    char method[16];
    char path[256]; 
    char version[16];


    int clientSocket = accept(fd, NULL, NULL);
    if(clientSocket < 0) {
        perror("accept failed");
        return 1;
    }

    int numberOfBytesRead = read(clientSocket, requestBuffer, sizeof(requestBuffer) - 1);

    if(numberOfBytesRead < 0) {
        perror("read failed");
        return 1;
    }

    requestBuffer[numberOfBytesRead] = '\0';

    printf("Received request:\n%s\n", requestBuffer);

    sscanf(requestBuffer, "%15s %255s %15s", method, path, version);

    printf("Method: %s\n", method);
    printf("Path: %s\n", path);   
    printf("Version: %s\n", version);

    char response[1024];

    if (strcmp(path, "/") == 0) {
        char *body = "<html><body><h1>Working!</h1></body></html>";
        snprintf(response, sizeof(response),
                 "HTTP/1.1 200 OK\r\n"
                 "Content-Type: text/plain\r\n"
                 "Content-Length: %zu\r\n"
                 "\r\n"
                 "%s", strlen(body), body);
    } else {
        char *body = "<html><body><h1>404 Not Found</h1></body></html>";
        snprintf(response, sizeof(response),
                "HTTP/1.1 404 Not Found\r\n"
                "Content-Type: text/plain\r\n"
                "Content-Length: %zu\r\n"
                "\r\n"
                "%s",
                strlen(body), body);
    }
    
    send(clientSocket, response, strlen(response), 0);


    close(clientSocket);

    return 0;
}