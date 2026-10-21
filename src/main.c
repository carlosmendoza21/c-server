#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include "server.h"
#include "request.h"



int main(void) {
    if (init_doc_root() < 0) {
        fprintf(stderr, "Could not find the document root. Run the server from the repo root.\n");
        return 1;
    }

    int fd = create_listen_socket(8080);
    if(fd < 0) {
        return 1;
    }

    signal(SIGPIPE, SIG_IGN);
    perma_serve(fd);


    close(fd);
    return 0;
}
