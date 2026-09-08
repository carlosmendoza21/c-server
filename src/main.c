#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include "server.h"
#include "request.h"



int main(int argc, char *argv[]) {
    int fd = create_listen_socket(8080);
    if(fd < 0) {
        return 1;
    }


    handle_requests(fd);


    close(fd);
    return 0;
}
