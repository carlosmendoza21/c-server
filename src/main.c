#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>


int main(int argc, char *argv[]) {
    int fd = create_listen_socket(8080);
    if(fd < 0) {
        return 1;
    }

    char requestBuffer[1024];

    char serMsg[255] = "Hello, World!";

    struct sockaddr_in serverAddr;

    int clienSck = accept(fd, NULL, NULL);
    if(clienSck < 0) {
        perror("accept failed");
        return 1;
    }

    int numberOfBytesRead = read(clienSck, requestBuffer, sizeof(requestBuffer) - 1);

    if(numberOfBytesRead < 0) {
        perror("read failed");
        return 1;
    }

    requestBuffer[numberOfBytesRead] = '\0';

    printf("Received request:\n%s\n", requestBuffer);



    send(clienSck, serMsg, strlen(serMsg), 0);

    close(fd);
    close(clienSck);   
    return 0;
}
