#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <netdb.h>
#include <unistd.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

#include <event.h>

#define MAXLEN 1024

struct rdma_event_channel       *channel = NULL;

char    recv_msg[MAXLEN] = "recive test!";

/******************************************************************************/

void get_connect_request(struct rdma_cm_id *listen_id) {
    struct rdma_cm_id   *id = NULL;
    struct ibv_mr       *mr = NULL;

    if (0 != rdma_get_request(listen_id, &id)) {
        perror("rmda_get_request");
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

void rdma_event_handle(int fd, short lib_event, void *arg) {
    struct rdma_cm_event *cm_event;
    if (0 != rdma_get_cm_event(channel, &cm_event)) {
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
    struct sockaddr_in          addr;
    struct rdma_cm_id           *listen_id = NULL;

    struct event_base           *base = NULL;
    struct event                *event_listen = NULL;

    uint16_t                    port = 0;

    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;

    if ( !(channel = rdma_create_event_channel()) ) {
        perror("rdma_create_event_channel");
        return -1;
    }

    if (0 != rdma_create_id(channel, &listen_id, NULL, RDMA_PS_TCP) )  {
        perror("rdma_create_id");
        return -1;
    }

    if (0 != rdma_bind_addr(listen_id, (struct sockaddr *)&addr)) {
        perror("rdma_bind_addr");
        return -1;
    }

    if (0 != rdma_listen(listen_id, 10)) {
        perror("rdma_listen");
        return -1;
    }

    port = ntohs(rdma_get_src_port(listen_id));

    printf("Listening on port %d\n", port);

    base = event_base_new();
    event_listen = calloc(1, sizeof(event_listen));

    // TODO
    event_set(event_listen, channel->fd, EV_READ | EV_PERSIST, rdma_event_handle, NULL);

    // Release resources 
    event_base_free(base);
    free(event_listen);

    rdma_destroy_id(listen_id);
    rdma_destroy_event_channel(channel);
    return 0;
}

