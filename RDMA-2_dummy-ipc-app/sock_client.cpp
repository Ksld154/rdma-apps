#include <stdio.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <iostream>
#include <string>

#include "ipc.h"

using namespace std;

class sock_client {
private:
    int                fd;
    struct sockaddr_un addr;
    char               buff[8192];
    // int                len;

public:
    sock_client(/* args */);
    ~sock_client();
    bool        conn();
    bool        send_data(std::string data);
    std::string recv_data();
    bool        disconn();
};

sock_client::sock_client(/* args */) {
    fd = -1;
    memset(&addr, 0, sizeof(addr));
    memset(buff, 0, sizeof(buff));
}

sock_client::~sock_client() {
}

bool sock_client::conn() {
    // Create socket
    if((fd = socket(PF_UNIX, SOCK_DGRAM, 0)) < 0) {
        perror("socket");
        return false;
    }

    // Bind socket
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, CLIENT_SOCK_FILE);
    unlink(CLIENT_SOCK_FILE);
    if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("bind");
        close(fd);
        return false;
    }

    // Connect socket to server
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, SERVER_SOCK_FILE);
    if(connect(fd, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("connect");
        close(fd);
        return false;
    }

    return true;
}

bool sock_client::send_data(std::string data) {
    strcpy(buff, data.c_str());
    if(send(fd, buff, strlen(buff) + 1, 0) == -1) {
        perror("send");
        close(fd);
        return false;
    }
    // printf("%s\n", data.c_str());

    return true;
}

std::string sock_client::recv_data() {
    // Receive response
    int len = 0;
    if((len = recv(fd, buff, 8192, 0)) < 0) {
        perror("recv");
        close(fd);
        return "";
    }
    // printf("receive %d bytes: %s\n", len, buff);
    printf("receive from sock_server: %s\n", buff);

    string resp = buff;
    return resp;
}

bool sock_client::disconn() {
    // clean up
    if(fd >= 0) {
        close(fd);
    }

    unlink(CLIENT_SOCK_FILE);

    return true;
}

// int main() {
//     sock_client client1;

//     client1.conn();
//     client1.send_data("5566 is the best!!");

//     string resp = client1.recv_data();
//     cout << resp << endl;

//     client1.disconn();

//     return 0;
// }
