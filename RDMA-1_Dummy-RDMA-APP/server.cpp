#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#include "common.h"
#include "dummy_app.h"
#include "messages.h"

#define MAX_FILE_NAME 256

struct timespec ts1, ts2;

struct conn_context {
    char *         buffer;
    struct ibv_mr *buffer_mr;

    struct message *msg;
    struct ibv_mr * msg_mr;

    int  fd;
    char file_name[MAX_FILE_NAME];

    char last_time_data[BUFFER_SIZE];
};

double get_elapsed_time(struct timespec t1, struct timespec t2) {
    double          timediff;
    struct timespec diff;

    if(t2.tv_nsec < t1.tv_nsec) {
        diff.tv_sec = (t2.tv_sec - 1) - t1.tv_sec;
        diff.tv_nsec = 1000000000 + t2.tv_nsec - t1.tv_nsec;
    } else {
        diff.tv_sec = t2.tv_sec - t1.tv_sec;
        diff.tv_nsec = t2.tv_nsec - t1.tv_nsec;
    }

    timediff = diff.tv_sec + (double)diff.tv_nsec / 1000000000.0;
    printf("Elapsed time: %lf s\n", timediff);

    return timediff;
}

// sending some control message or MR to client
static void send_message(struct rdma_cm_id *id) {
    struct conn_context *ctx = (struct conn_context *)id->context;

    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge     sge;

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.opcode = IBV_WR_SEND;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;

    sge.addr = (uintptr_t)ctx->msg;
    sge.length = sizeof(*ctx->msg);
    sge.lkey = ctx->msg_mr->lkey;

    TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}

static void send_respose(struct rdma_cm_id *id, uint32_t result) {
    struct conn_context *ctx = (struct conn_context *)id->context;

    struct ibv_send_wr wr, *bad_wr = NULL;
    struct ibv_sge     sge;

    memset(&wr, 0, sizeof(wr));

    wr.wr_id = (uintptr_t)id;
    wr.opcode = IBV_WR_SEND_WITH_IMM;
    wr.sg_list = &sge;
    wr.num_sge = 1;
    wr.send_flags = IBV_SEND_SIGNALED;
    wr.imm_data = htonl(result);

    sge.addr = (uintptr_t)ctx->msg;
    sge.length = sizeof(*ctx->msg);
    sge.lkey = ctx->msg_mr->lkey;

    TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}

// This is for client’s RDMA writes
static void post_receive(struct rdma_cm_id *id) {
    struct ibv_recv_wr wr, *bad_wr = NULL;

    memset(&wr, 0, sizeof(wr));

    // Incoming RDMA write requests will specify a target memory address
    // we don’t need to use sg_list and num_sge to specify a location in memory for the receive
    wr.wr_id = (uintptr_t)id;
    wr.sg_list = NULL;
    wr.num_sge = 0;

    TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
}

// In on_pre_conn(), we allocate a structure to contain various connection context fields
// (a buffer to contain data from the client, a buffer from which to send messages to the client, etc.)
// and post a receive work request for the client’s RDMA writes
static void on_pre_conn(struct rdma_cm_id *id) {
    struct conn_context *ctx = (struct conn_context *)malloc(sizeof(struct conn_context));

    id->context = ctx;
    ctx->file_name[0] = '\0';  // take this to mean we don't have the file name

    memset(ctx->last_time_data, 0, sizeof(ctx->last_time_data));

    // buffer for receiving client's data chunk
    posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), BUFFER_SIZE);  // similar with malloc()
    TEST_Z(ctx->buffer_mr = ibv_reg_mr(rc_get_pd(), ctx->buffer, BUFFER_SIZE, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE));

    // memory region for RECV queue
    posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
    TEST_Z(ctx->msg_mr = ibv_reg_mr(rc_get_pd(), ctx->msg, sizeof(*ctx->msg), 0));

    post_receive(id);
}

// After the connection is established, on_connection() sends the memory region details to the client,
// so that client can directly send data chunk into remote buffer
static void on_connection(struct rdma_cm_id *id) {
    struct conn_context *ctx = (struct conn_context *)id->context;

    ctx->msg->id = MSG_MR;
    ctx->msg->data.mr.addr = (uintptr_t)ctx->buffer_mr->addr;
    ctx->msg->data.mr.rkey = ctx->buffer_mr->rkey;
    clock_gettime(CLOCK_REALTIME, &ts1);

    send_message(id);
}

static void on_completion(struct ibv_wc *wc) {
    struct rdma_cm_id *  id = (struct rdma_cm_id *)(uintptr_t)wc->wr_id;
    struct conn_context *ctx = (struct conn_context *)id->context;

    if(wc->opcode == IBV_WC_RECV_RDMA_WITH_IMM) {
        uint32_t size = ntohl(wc->imm_data);

        if(size == 1) {
            ctx->msg->id = MSG_DONE;
            send_message(id);

            // don't need post_receive() since we're done with this connection
        } else {
            printf("Received data: %s\n", ctx->buffer);
            // strncpy(ctx->last_time_data, ctx->buffer, strlen(ctx->buffer) + 1);  // cached sent data

            // execute dummy app
            int res = add_one(ctx->buffer);
            post_receive(id);

            ctx->msg->id = MSG_READY;
            send_respose(id, res);
            // send_message(id);
        }
    }
}

static void on_disconnect(struct rdma_cm_id *id) {
    struct conn_context *ctx = (struct conn_context *)id->context;
    close(ctx->fd);

    clock_gettime(CLOCK_REALTIME, &ts2);

    // get_elapsed_time(ts1, ts2);
    // printf("Elapsed time: %lf s\n", ((double) (transfer_end-transfer_start)) / CLOCKS_PER_SEC);

    ibv_dereg_mr(ctx->buffer_mr);
    ibv_dereg_mr(ctx->msg_mr);

    free(ctx->buffer);
    free(ctx->msg);

    printf("Finished transferring %s\n\n", ctx->file_name);

    free(ctx);
}

int main(int argc, char **argv) {
    rc_init(
        on_pre_conn,
        on_connection,
        on_completion,
        on_disconnect);

    printf("waiting for connections. interrupt (^C) to exit.\n");

    rc_server_loop(DEFAULT_PORT);

    return 0;
}
