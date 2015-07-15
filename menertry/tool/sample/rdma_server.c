/*
 * Copyright (c) 2005-2009 Intel Corporation.  All rights reserved.
 *
 * This software is available to you under the OpenIB.org BSD license
 * below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AWV
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <getopt.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>

static char *port = "7471";

#define SEND_MSG "message from send\n"
#define RECV_MSG "message from recv\n"

struct rdma_cm_id *listen_id, *id;
struct ibv_mr *mr;
char send_msg[] = SEND_MSG;
char recv_msg[] = RECV_MSG;

static int run(void)
{
	struct rdma_addrinfo hints, *res;
	struct ibv_qp_init_attr attr;
	struct ibv_wc wc;
	int ret;

	memset(&hints, 0, sizeof hints);
	hints.ai_flags = RAI_PASSIVE;
	hints.ai_port_space = RDMA_PS_TCP;
	ret = rdma_getaddrinfo(NULL, port, &hints, &res);
	if (ret) {
		printf("rdma_getaddrinfo %d\n", errno);
		return ret;
	}

	memset(&attr, 0, sizeof attr);
	attr.cap.max_send_wr = attr.cap.max_recv_wr = 1;
	attr.cap.max_send_sge = attr.cap.max_recv_sge = 1;
	attr.cap.max_inline_data = 16;
	attr.sq_sig_all = 1;
	ret = rdma_create_ep(&listen_id, res, NULL, &attr);
	rdma_freeaddrinfo(res);
	if (ret) {
		printf("rdma_create_ep %d\n", errno);
		return ret;
	}

	ret = rdma_listen(listen_id, 0);
	if (ret) {
		printf("rdma_listen %d\n", errno);
		return ret;
	}

	ret = rdma_get_request(listen_id, &id);
	if (ret) {
		printf("rdma_get_request %d\n", errno);
		return ret;
	}

	mr = rdma_reg_msgs(id, recv_msg, sizeof(RECV_MSG));
	if (!mr) {
		printf("rdma_reg_msgs %d\n", errno);
		return ret;
	}

	ret = rdma_post_recv(id, NULL, recv_msg, sizeof(RECV_MSG), mr);
	if (ret) {
		printf("rdma_post_recv %d\n", errno);
		return ret;
	}

	ret = rdma_accept(id, NULL);
	if (ret) {
		printf("rdma_accept %d\n", errno);
		return ret;
	}

	ret = rdma_get_recv_comp(id, &wc);
	if (ret <= 0) {
		printf("rdma_get_recv_comp %d\n", ret);
		return ret;
	}

	ret = rdma_post_send(id, NULL, send_msg, sizeof(SEND_MSG), NULL, IBV_SEND_INLINE);
	if (ret) {
		printf("rdma_post_send %d\n", errno);
		return ret;
	}

	ret = rdma_get_send_comp(id, &wc);
	if (ret <= 0) {
		printf("rdma_get_send_comp %d\n", ret);
		return ret;
	}

	printf("send msg : %s\n", send_msg);
	printf("recv msg : %s\n", recv_msg);

	rdma_disconnect(id);
	rdma_dereg_mr(mr);
	rdma_destroy_ep(id);
	rdma_destroy_ep(listen_id);
	return 0;
}

int main(int argc, char **argv)
{
	int op, ret;

	while ((op = getopt(argc, argv, "p:")) != -1) {
		switch (op) {
		case 'p':
			port = optarg;
			break;
		default:
			printf("usage: %s\n", argv[0]);
			printf("\t[-p port_number]\n");
			exit(1);
		}
	}

	printf("rdma_server: start\n");
	ret = run();
	printf("rdma_server: end %d\n", ret);
	return ret;
}
