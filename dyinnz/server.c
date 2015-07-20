#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include <event.h>

#define MAXLEN 1024

struct Setting {
    int         cq_number;
    int         listen_port; 
};

struct RDMAContext {
    struct ibv_context          *device_context;
    struct ibv_comp_channel     *comp_channel;
    struct ibv_pd               *pd;
    struct ibv_cq               *cq;
    
    struct rdma_event_channel   *cm_channel;
    struct rdma_cm_id           *listen_id;

    struct event_base           *base;
    struct event                listen_event;
};

void rdma_cm_event_handle(int fd, short lib_event, void *arg);

/******************************************************************************
 * Test
 *
 *****************************************************************************/

char    recv_msg[MAXLEN] = "recive test!";
struct RDMAContext *rdma_context = NULL;

/******************************************************************************
 * Description
 * Init struct Setting with default
 *
 *****************************************************************************/
void 
init_setting_with_default(struct Setting *setting) {
    setting->cq_number = 1024;
    setting->listen_port = 5555;
}

/******************************************************************************
 *
 * Description
 * Release all resources
 *
 ******************************************************************************/
 
void release_resources(struct Setting *setting, struct RDMAContext *context) {
    event_base_free(context->base);

    rdma_destroy_id(context->listen_id); 
    rdma_destroy_event_channel(context->cm_channel);

    ibv_destroy_cq(context->cq);
    ibv_dealloc_pd(context->pd);
    ibv_destroy_comp_channel(context->comp_channel);

    free(setting);
    free(context);
}

/******************************************************************************
 * Return
 * 0 on success, -1 on failure
 *
 * Description
 * Init resources required for listening, and build listeninng.
 *
 *****************************************************************************/
int
init_rdma_listen(struct Setting *setting, struct RDMAContext *context) {
    struct sockaddr_in          addr;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(setting->listen_port);

    if ( !(context->cm_channel = rdma_create_event_channel()) ) {
        perror("rdma_create_event_channel");
        return -1;
    }

    if (0 != rdma_create_id(context->cm_channel, &context->listen_id, NULL, RDMA_PS_TCP) )  {
        perror("rdma_create_id");
        return -1;
    }

    if (0 != rdma_bind_addr(context->listen_id, (struct sockaddr *)&addr)) {
        perror("rdma_bind_addr");
        return -1;
    }

    if (0 != rdma_listen(context->listen_id, 0)) {
        perror("rdma_listen");
        return -1;
    }

    printf("Listening on port %d\n", ntohs(rdma_get_src_port(context->listen_id)) );

    // Set ibv_context
    context->device_context = context->listen_id->verbs;

    return 0;
}

/******************************************************************************
 * Return
 * 0 on success, -1 on failure
 *
 * Description
 * Create shared resources for RDMA operations
 *
 *****************************************************************************/
int
init_rdma_shared_resources(struct Setting *setting, struct RDMAContext *context) {

    if ( !(context->comp_channel = ibv_create_comp_channel(context->device_context)) ) {
        perror("ibv_create_comp_channel");
        return -1;
    }

    if ( !(context->pd = ibv_alloc_pd(context->device_context)) ) {
        perror("ibv_alloc_pd");
        return -1;
    }

    if ( !(context->cq = ibv_create_cq(context->device_context, 
                    setting->cq_number, NULL, context->comp_channel, 0)) ) {
        perror("ibv_create_cq");
        return -1;
    }

    return 0;
}

/******************************************************************************
 * Description
 * Create shared resources for RDMA operations
 *
 *****************************************************************************/
int
init_and_dispatch_event(struct RDMAContext *context) {
    context->base = event_base_new();
    memset(&context->listen_event, 0, sizeof(struct event));

    // TODO
    event_set(&context->listen_event, context->cm_channel->fd, EV_READ | EV_PERSIST, 
            rdma_cm_event_handle, NULL);
    event_base_set(context->base, &context->listen_event);
    event_add(&context->listen_event, NULL);

    // main loop
    event_base_dispatch(context->base);

    return 0;
}

/******************************************************************************
 * Description
 *
 *****************************************************************************/
void 
get_connect_request(struct rdma_cm_id *id) {
    struct ibv_mr           *mr = NULL;
    struct ibv_qp_init_attr attr;

    memset(&attr, 0, sizeof(attr));
    attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
    attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
    attr.cap.max_inline_data = MAXLEN;
    attr.sq_sig_all = 1;
    attr.qp_type = IBV_QPT_RC;

    // TODO: release it
    if (0 != rdma_create_qp(id, NULL, &attr)) {
        perror("rdma_create_qp");
        return;
    }

    if ( !(mr = rdma_reg_msgs(id, recv_msg, MAXLEN)) ) {
        perror("rdma_reg_msgs");
        return;
    }

    if (0 != rdma_post_recv(id, NULL, recv_msg, MAXLEN, mr)) {
        perror("rdma_post_recv");
        return;
    }

    if (0 != rdma_accept(id, NULL)) {
        perror("rdma_accept");
        return;
    }

    printf("Establish ok!\n");
}

/******************************************************************************
 * Description
 *
 *****************************************************************************/
void 
rdma_cm_event_handle(int fd, short lib_event, void *arg) {
    struct rdma_cm_event *cm_event;
    if (0 != rdma_get_cm_event(rdma_context->cm_channel, &cm_event)) {
        perror("rdma_get_cm_event");
        return;
    }

    printf("---> %s\n", rdma_event_str(cm_event->event));

    switch (cm_event->event) {
        case RDMA_CM_EVENT_CONNECT_REQUEST:
            get_connect_request(cm_event->id);
            break;

        case RDMA_CM_EVENT_ESTABLISHED:
            break;

        case RDMA_CM_EVENT_DISCONNECTED:
            break;

        default:
            break;
    }

    rdma_ack_cm_event(cm_event);
}

int main(int argc, char *argv[]) {
    struct Setting      *setting = calloc(1, sizeof(struct Setting));
    struct RDMAContext  *context = calloc(1, sizeof(struct RDMAContext)); 
    rdma_context = context;

    init_setting_with_default(setting);

    if (0 != init_rdma_listen(setting, context)) return -1;
    if (0 != init_rdma_shared_resources(setting, context)) return -1;
    if (0 != init_and_dispatch_event(context)) return -1;

    release_resources(setting, context);
    return 0;
}


