#include "rdma-common.h"

static const int RDMA_BUFFER_SIZE = 1024;

struct message {
	enum {
		MSG_MR,
		MSG_DONE
	} type;

	union {
		struct ibv_mr mr;
	} data;
};

struct context {
	struct ibv_context *ctx;
	struct ibv_pd *pd;
	struct ibv_cq *cq;
	struct ibv_comp_channel *comp_channel;

	pthread_t cq_poller_thread;
};

struct connection {
	struct rdma_cm_id *id;
	struct ibv_qp *qp;

	int connected;

	struct ibv_mr *recv_mr;
	struct ibv_mr *send_mr;
	struct ibv_mr *rdma_local_mr;
	struct ibv_mr *rdma_remote_mr;

	struct ibv_mr peer_mr;

	struct message *recv_msg;
	struct message *send_msg;

	char *rdma_local_region;
	char *rdma_remote_region;

	enum {
		SS_INIT,
		SS_MR_SENT,
		SS_RDMA_SENT,
		SS_DONE_SENT
	} send_state;

	enum {
		RS_INIT,
		RS_MR_RECV,
		RS_DONE_RECV
	} recv_state;
};

static void build_context(struct ibv_context *verbs);
static void build_qp_attr(struct ibv_qp_init_attr *qp_attr);
static char * get_peer_message_region(struct connection *conn);
static void on_completion(struct ibv_wc *);
static void * poll_cq(void *);
static void post_receives(struct connection *conn);
static void register_memory(struct connection *conn);
static void send_message(struct connection *conn);

static struct context *s_ctx = NULL;
static enum mode s_mode = M_WRITE;

void die(const char *reason) {
	fprintf(stderr, "%s\n", reason);
	exit(EXIT_FAILURE);
}

// 1. Build verb context, create a protection domain, completion queue
// 2. Create send-receive queue pair, and register memory region
// 3. Pre-post RECV WR 
void build_connection(struct rdma_cm_id *id) {
	struct connection *conn;
	struct ibv_qp_init_attr qp_attr;

	build_context(id->verbs);
	build_qp_attr(&qp_attr);

	TEST_NZ(rdma_create_qp(id, s_ctx->pd, &qp_attr));

	id->context = conn = (struct connection *)malloc(sizeof(struct connection));

	conn->id = id;
	conn->qp = id->qp;

	conn->send_state = SS_INIT;
	conn->recv_state = RS_INIT;

	conn->connected = 0;

	register_memory(conn);
	post_receives(conn);
}

void build_context(struct ibv_context *verbs) {
	if (s_ctx) {
		if (s_ctx->ctx != verbs)
			die("cannot handle events in more than one context.");

		return;
	}

	s_ctx = (struct context *)malloc(sizeof(struct context));

	s_ctx->ctx = verbs;

	TEST_Z(s_ctx->pd = ibv_alloc_pd(s_ctx->ctx));
	TEST_Z(s_ctx->comp_channel = ibv_create_comp_channel(s_ctx->ctx));
	TEST_Z(s_ctx->cq = ibv_create_cq(s_ctx->ctx, 10, NULL, s_ctx->comp_channel, 0)); /* cqe=10 is arbitrary */
	TEST_NZ(ibv_req_notify_cq(s_ctx->cq, 0));

	TEST_NZ(pthread_create(&s_ctx->cq_poller_thread, NULL, poll_cq, NULL));
}

// Control the number of simultaneous outstanding RDMA read requests
void build_params(struct rdma_conn_param *params) {
	memset(params, 0, sizeof(*params));

	params->initiator_depth = params->responder_resources = 1;

	// Infinite retry if the peer responds with a receiver-not-ready (RNR) error 
	// i.e. SEND WR is posted before a corresponding RECV WR is posted on the peer
	params->rnr_retry_count = 7; 
}

void build_qp_attr(struct ibv_qp_init_attr *qp_attr) {
	memset(qp_attr, 0, sizeof(*qp_attr));

	qp_attr->send_cq = s_ctx->cq;
	qp_attr->recv_cq = s_ctx->cq;
	qp_attr->qp_type = IBV_QPT_RC;

	qp_attr->cap.max_send_wr = 10;
	qp_attr->cap.max_recv_wr = 10;
	qp_attr->cap.max_send_sge = 1;
	qp_attr->cap.max_recv_sge = 1;
}

void destroy_connection(void *context) {
	struct connection *conn = (struct connection *)context;

	rdma_destroy_qp(conn->id);

	ibv_dereg_mr(conn->send_mr);
	ibv_dereg_mr(conn->recv_mr);
	ibv_dereg_mr(conn->rdma_local_mr);
	ibv_dereg_mr(conn->rdma_remote_mr);

	free(conn->send_msg);
	free(conn->recv_msg);
	free(conn->rdma_local_region);
	free(conn->rdma_remote_region);

	rdma_destroy_id(conn->id);

	free(conn);
}

void * get_local_message_region(void *context) {
	if (s_mode == M_WRITE)  // write local msg to remote
		return ((struct connection *)context)->rdma_local_region;
	else  // get remote msg. to local
		return ((struct connection *)context)->rdma_remote_region;
}

char * get_peer_message_region(struct connection *conn) {
	if (s_mode == M_WRITE)  // write local msg to remote
		return conn->rdma_remote_region;
	else
		return conn->rdma_local_region;
}


// Main part!! The polling thread detects a WR is completed
void on_completion(struct ibv_wc *wc) {
	struct connection *conn = (struct connection *)(uintptr_t)wc->wr_id;

	if (wc->status != IBV_WC_SUCCESS)
		die("on_completion: status is not IBV_WC_SUCCESS.");

	// If the completed operation is a RECV operation, then recv_state is incremented.
	if (wc->opcode & IBV_WC_RECV) {
		conn->recv_state++;
		
		// If the received message is MSG_MR, 
		// we copy the received MR into our connection structure’s peer_mr member, 
		// and rearm the receive slot (insert a RECV to WQ?)
		if (conn->recv_msg->type == MSG_MR) {
			memcpy(&conn->peer_mr, &conn->recv_msg->data.mr, sizeof(conn->peer_mr));
			post_receives(conn); /* only rearm for MSG_MR */
			// printf("Remote memory addr: %s\n", (char*)conn->peer_mr.addr);
			printf("Remote memory addr: 0x%" PRIx64 "\n", (u_int64_t)conn->peer_mr.addr);
			printf("Remote memory block length: %zu\n", conn->peer_mr.length);

			/* received peer's MR before sending ours, so send ours back */
			if (conn->send_state == SS_INIT) 
				send_mr(conn);
		}

	} else {
		conn->send_state++;
		if(conn->send_state == SS_MR_SENT)
			printf("MSG_MR send completed successfully.\n");
		else if(conn->send_state == SS_RDMA_SENT)
			printf("RDMA_OPs send completed successfully.\n");
		else if(conn->send_state == SS_DONE_SENT)
			printf("MSG_DONE send completed successfully.\n");
	}
	
	// If we’ve both sent our MR and received the peer’s MR. 
	// This indicates that we’re ready to post an RDMA operation and post MSG_DONE
	if (conn->send_state == SS_MR_SENT && conn->recv_state == RS_MR_RECV) {
		struct ibv_send_wr wr, *bad_wr = NULL;
		struct ibv_sge sge;

		if (s_mode == M_WRITE)
			printf("received MSG_MR. writing message to remote memory...\n");
		else
			printf("received MSG_MR. reading message from remote memory...\n");

		memset(&wr, 0, sizeof(wr));

		// Building an RDMA work request
		wr.wr_id = (uintptr_t)conn;
		wr.opcode = (s_mode == M_WRITE) ? IBV_WR_RDMA_WRITE : IBV_WR_RDMA_READ;
		wr.sg_list = &sge;
		wr.num_sge = 1;
		wr.send_flags = IBV_SEND_SIGNALED;
		
		// Pass the peer’s RDMA address/key, prove we have access right to remote blocks
		// not required to use conn->peer_mr.addr for remote_addr, 
		// could use any address falling within the bounds of the memory region registered with ibv_reg_mr()
		wr.wr.rdma.remote_addr = (uintptr_t)conn->peer_mr.addr; 
		wr.wr.rdma.rkey = conn->peer_mr.rkey;

		sge.addr = (uintptr_t)conn->rdma_local_region;
		sge.length = RDMA_BUFFER_SIZE;
		sge.lkey = conn->rdma_local_mr->lkey;

		// Post RDMA READ/WRITE operation!
		TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));

		conn->send_msg->type = MSG_DONE;
		send_message(conn);
	} 

	// This indicating that we’ve sent MSG_DONE and received MSG_DONE from the peer. 
	// It means it is safe to print the message buffer and disconnect
	else if (conn->send_state == SS_DONE_SENT && conn->recv_state == RS_DONE_RECV) {
		// printf("send_state: %d\n", conn->send_state);
		printf("remote buffer: %s\n", get_peer_message_region(conn));
		rdma_disconnect(conn->id);
	}
}

void on_connect(void *context) {
	((struct connection *)context)->connected = 1;
}

void * poll_cq(void *ctx) {
	struct ibv_cq *cq;
	struct ibv_wc wc;

	while (1) {
		TEST_NZ(ibv_get_cq_event(s_ctx->comp_channel, &cq, &ctx));
		ibv_ack_cq_events(cq, 1);
		TEST_NZ(ibv_req_notify_cq(cq, 0));

		while (ibv_poll_cq(cq, 1, &wc))
			on_completion(&wc);
	}

	return NULL;
}

void post_receives(struct connection *conn) {
	struct ibv_recv_wr wr, *bad_wr = NULL;
	struct ibv_sge sge;

	wr.wr_id = (uintptr_t)conn;
	wr.next = NULL;
	wr.sg_list = &sge;
	wr.num_sge = 1;

	sge.addr = (uintptr_t)conn->recv_msg;
	sge.length = sizeof(struct message);
	sge.lkey = conn->recv_mr->lkey;

	TEST_NZ(ibv_post_recv(conn->qp, &wr, &bad_wr));
}

void register_memory(struct connection *conn) {
	conn->send_msg = malloc(sizeof(struct message));
	conn->recv_msg = malloc(sizeof(struct message));

	conn->rdma_local_region = malloc(RDMA_BUFFER_SIZE);
	conn->rdma_remote_region = malloc(RDMA_BUFFER_SIZE);

	memset(conn->rdma_local_region, 0, RDMA_BUFFER_SIZE);
	memset(conn->rdma_remote_region, 0, RDMA_BUFFER_SIZE);

	// Memory region for local SEND queue
	TEST_Z(conn->send_mr = ibv_reg_mr(
		s_ctx->pd, 
		conn->send_msg, 
		sizeof(struct message), 
		0));  // LOCAL_READ is enabled by default

	// Memory region for local RECV queue
	TEST_Z(conn->recv_mr = ibv_reg_mr(
		s_ctx->pd, 
		conn->recv_msg, 
		sizeof(struct message), 
		IBV_ACCESS_LOCAL_WRITE));

	// Memory region for READ/WRITE to local RDMA memory blocks
	TEST_Z(conn->rdma_local_mr = ibv_reg_mr(
		s_ctx->pd, 
		conn->rdma_local_region, 
		RDMA_BUFFER_SIZE, 
		((s_mode == M_WRITE) ? 0 : IBV_ACCESS_LOCAL_WRITE)));
		// for RDMA write: we read local memory content, and then write it to remote memory. 
		//                 Therefore we only need LOCAL_READ for local MR
		// for RDMA read:  we read remote memory content, and then write it to local memory. 
		//                 Therefore we need LOCAL_WRITE for local MR

	// Memory region for READ/WRITE to remote RDMA memory blocks
	TEST_Z(conn->rdma_remote_mr = ibv_reg_mr(
		s_ctx->pd, 
		conn->rdma_remote_region, 
		RDMA_BUFFER_SIZE, 
		((s_mode == M_WRITE) ? (IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE) : IBV_ACCESS_REMOTE_READ)));
		// for RDMA write: we perform a remote write, so we need IBV_ACCESS_REMOTE_WRITE along with LOCAL_WRITE(it's necessary from manual)
		// for RDMA read:  we perform a remote read, so we need IBV_ACCESS_REMOTE_READ
}

void send_message(struct connection *conn) {
	struct ibv_send_wr wr, *bad_wr = NULL;
	struct ibv_sge sge;

	memset(&wr, 0, sizeof(wr));

	wr.wr_id = (uintptr_t)conn;
	wr.opcode = IBV_WR_SEND;
	wr.sg_list = &sge;
	wr.num_sge = 1;
	wr.send_flags = IBV_SEND_SIGNALED;

	sge.addr = (uintptr_t)conn->send_msg;
	sge.length = sizeof(struct message);
	sge.lkey = conn->send_mr->lkey;

	while (!conn->connected);

	TEST_NZ(ibv_post_send(conn->qp, &wr, &bad_wr));
}

// Sends memory descriptors(address and access key) to peer
void send_mr(void *context) {
	struct connection *conn = (struct connection *)context;

	conn->send_msg->type = MSG_MR;
	memcpy(&conn->send_msg->data.mr, conn->rdma_remote_mr, sizeof(struct ibv_mr));

	send_message(conn);
}

void set_mode(enum mode m) {
	s_mode = m;
}
