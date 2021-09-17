#ifndef RDMA_MESSAGES_H
#define RDMA_MESSAGES_H

#define BUFFER_SIZE 10240

const char *DEFAULT_PORT = "12345";
const int   REQUEST_LIMIT = 5;
const int   SAME_MESSAGE_PROBABILITY_INVERSE = 2;

enum message_id {
    MSG_INVALID = 0,
    MSG_MR,
    MSG_READY,
    MSG_DONE
};

struct message {
    int id;

    union {
        struct {
            uint64_t addr;
            uint32_t rkey;
        } mr;
    } data;
};

#endif
