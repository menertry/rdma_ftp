#include <stdlio.h>
#include <stdlib.h>
#include <errno.h>
#include <netdb.h>
#include <rdma/rmda_cma.h>
#include <rdma/rdma_verbs.h>

static const char *port = "7471";
static const int kMaxRecvWr = 1024;
static const int kMaxSendWr = 1024;
static const int kMaxInlineData = 1024;

static int run() {
    struct rdma_addrinfo    hints,
                            *res = NULL;
    struct rdma_cm_id       *listen_id = NULL,
                            *id = NULL;
    struct ibv_qp_init_attr attr;
    struct ibv_wc           wc;
    struct ibv_mr           *mr = NULL;

    int ret;

    memset(&hints, 0, sizeof(hints));
    hints.ai_flags = RAI_PASSIVE;
    hints.ai_port_space = RDMA_PS_TCP;

    if ( 0 != (ret = rdma_getaddrinfo(NULL, port, &hints, &res)) ) {
        perror("rdma_getaddrinfo");
        return -1;
    }

    memset(&attr, 0, sizeof(attr));
    attr.cap.max_send_wr = kMaxSendWr;
    attr.cap.max_recv_wr = kMaxRecvWr;
    attr.cap.max_recv_sge = attr.cap.max_send_sge = 1;
    attr.cap.max_inline_data = kMaxInlineData;

    if ( 0 != (ret = rdma_create_ep(&listen_id, res, NULL, &attr)) ) {
        perror("rdma_create_ep");
        return -1;
    }

    rdma_freeaddrinfo(res);
    
    if ( 0 != (ret = rdma_listen(listen_id, 0)) ) {
        perror("rdma_listen"); 
        return -1;
    }
}

int main() {
}
