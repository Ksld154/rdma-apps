#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <string>

#include "ipc.h"

int main() {
    int                fd;
    struct sockaddr_un addr;
    int                ret;
    char               buff[8192];
    // int                ok = 1;
    // int                err = 0;
    int                len;
    struct sockaddr_un from;
    socklen_t          fromlen = sizeof(from);

    // Create socket
    if((fd = socket(PF_UNIX, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        return 1;
    }

    // Bind socket
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SERVER_SOCK_FILE);
    unlink(SERVER_SOCK_FILE);
    if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return 1;
    }

    while((len = recvfrom(fd, buff, 8192, 0, (struct sockaddr *)&from, &fromlen)) > 0) {
        printf("recv from sock client: %s\n", buff);
        int payload = atoi(buff);
        payload++;
        printf("calculate result: %d\n", payload);

        std::string resp = std::to_string(payload);
        strcpy(buff, resp.c_str());
        ret = sendto(fd, buff, strlen(buff) + 1, 0, (struct sockaddr *)&from, fromlen);
        if(ret < 0) {
            perror("sendto");
            break;
        }
    }

    if(fd >= 0) {
        close(fd);
    }

    return 0;
}
