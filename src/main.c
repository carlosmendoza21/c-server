#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>


int main(int argc, char *argv[]) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);

    char serMsg[255] = "Hello, World!";

    struct sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));

    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY); 

    bind(fd, (struct sockaddr *)&serverAddr, sizeof(serverAddr));

    listen(fd, 1);

    int clienSck = accept(fd, NULL, NULL);

    send(clienSck, serMsg, strlen(serMsg), 0);

    close(fd);
    close(clienSck);   
    return 0;
}