#include <fcntl.h>
#include <libgen.h>

#include "common.h"
#include "messages.h"

struct client_context {
	char *buffer;
	struct ibv_mr *buffer_mr;

	struct message *msg;
	struct ibv_mr *msg_mr;

	uint64_t peer_addr;
	uint32_t peer_rkey;

	int fd;
	const char *file_name;


	int 	payload_idx;
	char 	*payload;
};

char last_time_data[BUFFER_SIZE];

static void write_remote(struct rdma_cm_id *id, uint32_t len) {
	struct client_context *ctx = (struct client_context *)id->context;
	struct ibv_send_wr wr, *bad_wr = NULL;
	struct ibv_sge sge;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id = (uintptr_t)id;
	wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
	wr.send_flags = IBV_SEND_SIGNALED;
	wr.imm_data = htonl(len);  // data(buffer) length
	wr.wr.rdma.remote_addr = ctx->peer_addr;
	wr.wr.rdma.rkey = ctx->peer_rkey;

	if (len) {
		wr.sg_list = &sge;
		wr.num_sge = 1;

		sge.addr = (uintptr_t)ctx->buffer;
		sge.length = len;
		sge.lkey = ctx->buffer_mr->lkey;
	}

	TEST_NZ(ibv_post_send(id->qp, &wr, &bad_wr));
}

// for receiving server's control message
static void post_receive(struct rdma_cm_id *id) {
	struct client_context *ctx = (struct client_context *)id->context;
	struct ibv_recv_wr wr, *bad_wr = NULL;
	struct ibv_sge sge;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id = (uintptr_t)id;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	// a buffer large enough to hold a struct message
	sge.addr = (uintptr_t)ctx->msg;
	sge.length = sizeof(*ctx->msg);
	sge.lkey = ctx->msg_mr->lkey;

	TEST_NZ(ibv_post_recv(id->qp, &wr, &bad_wr));
}


static void send_data_chunk(struct rdma_cm_id *id, int idx) {
	struct client_context *ctx = (struct client_context *)id->context;
	
	char data[BUFFER_SIZE];
	memset(data, 0, sizeof(data));
	
	if(idx >= REQUEST_LIMIT) {
		strncpy(ctx->buffer, data, strlen(data)+1);
		write_remote(id, strlen(data)+1);  //+1 for trailing '\0'
		return;
	}

	// send last time's data
	if(idx > 0 && rand() % SAME_MESSAGE_PROBABILITY_INVERSE == 0) {
		strncpy(data, last_time_data, strlen(last_time_data)+1);
		printf("same\n");
	} else { // send new data
		sprintf(data, "[%d] message from active/client side", idx);
	}
	
	strncpy(ctx->buffer, data, strlen(data)+1);
	write_remote(id, strlen(data)+1); //+1 for trailing '\0'
	
	strncpy(last_time_data, data, strlen(data)+1);  // cached sent data
}

static void on_pre_conn(struct rdma_cm_id *id) {
	struct client_context *ctx = (struct client_context *)id->context;

	// for buffering the data chucks that are sending to server
	posix_memalign((void **)&ctx->buffer, sysconf(_SC_PAGESIZE), BUFFER_SIZE);
	TEST_Z(ctx->buffer_mr = ibv_reg_mr(rc_get_pd(), ctx->buffer, BUFFER_SIZE, 0));

	// for receiving server's control message
	posix_memalign((void **)&ctx->msg, sysconf(_SC_PAGESIZE), sizeof(*ctx->msg));
	TEST_Z(ctx->msg_mr = ibv_reg_mr(rc_get_pd(), ctx->msg, sizeof(*ctx->msg), IBV_ACCESS_LOCAL_WRITE));

	post_receive(id);
}

static void on_completion(struct ibv_wc *wc) {
	struct rdma_cm_id *id = (struct rdma_cm_id *)(uintptr_t)(wc->wr_id);
	struct client_context *ctx = (struct client_context *)id->context;

	if (wc->opcode & IBV_WC_RECV) {
		if (ctx->msg->id == MSG_MR) {
			ctx->peer_addr = ctx->msg->data.mr.addr;
			ctx->peer_rkey = ctx->msg->data.mr.rkey;

			printf("received MR, sending file name\n");
			ctx->payload_idx = 0;

			send_data_chunk(id, ctx->payload_idx++);

		} else if (ctx->msg->id == MSG_READY) {
			printf("received READY, sending chunk\n");
			
			usleep(100);
			send_data_chunk(id, ctx->payload_idx++);
			
		} else if (ctx->msg->id == MSG_DONE) {
			printf("received DONE, disconnecting\n");
			rc_disconnect(id);
			return;
		}

		post_receive(id);
	}
}

int main(int argc, char **argv) {
	
	if (argc != 3) {
		fprintf(stderr, "usage: %s <server-address> <file-name>\n", argv[0]);
		return 1;
	}

	struct client_context ctx;
	// ctx.file_name = basename(argv[2]);
	
	// ctx.fd = open(argv[2], O_RDONLY);
	// if (ctx.fd == -1) {
	// 	fprintf(stderr, "unable to open input file \"%s\"\n", ctx.file_name);
	// 	return 1;
	// }
	memset(last_time_data, 0, sizeof(last_time_data));
	srand(time(NULL));

	rc_init(
		on_pre_conn,
		NULL, // on connect
		on_completion,
		NULL); // on disconnect

	rc_client_loop(argv[1], DEFAULT_PORT, &ctx);

	close(ctx.fd);

	return 0;
}

