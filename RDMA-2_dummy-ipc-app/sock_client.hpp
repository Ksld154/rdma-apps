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