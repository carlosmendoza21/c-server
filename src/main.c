#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>


int main(int argc, char *argv[]) {
    int opt = 1;
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    char requestBuffer[1024];

    char serMsg[255] = "Hello, World!";

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); 

    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if(bind(fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("bind failed");
        return 1;
    }

    if(listen(fd, 1) < 0) {
        perror("listen failed");
        return 1;
    }

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

