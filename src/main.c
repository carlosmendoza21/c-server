#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "server.h"
#include "request.h"



int main(void) {
    int fd = create_listen_socket(8080);
    if(fd < 0) {
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);
    perma_serve(fd);


    close(fd);
    return 0;
}
