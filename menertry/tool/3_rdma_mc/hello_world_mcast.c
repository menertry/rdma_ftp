/*
In order to receive messages destined to a multicast group one must register to it and attach a QP to the group. 
In order to join a multicast group one must know the group's GID and send an SA MAD asking to join the group, 
so opensm must be running
After a successful join, a LID will be returned in the MAD. If one wishes to post SRs to the multicast group, he must
use this LID.
In order to receive messages sent to the multicast group a QP must be attached to the group, using that LID.

Important notes:
---------------
1. when coming to use multicast one must pay attention to the following device attributes:
	max_mcast_grp 		 	- Maximum number of supported multicast groups per device
	max_mcast_qp_attach		- Maximum number of QPs that can be attached to a  multicast group 
	max_total_mcast_qp_attach	- Maximum number of QPs which can be attached to multicast groups for the entire decvice

2. although only one QP is attached to the multicast group in this code example, every client can attach any number of QPs to the same multicast group  
	up to max_mcast_qp_attach
3. if there is more then one process running on the same machine and wishes to use the same multicast group, 
	it's enough that only one process joins the multicast group. after that all the other processes can 
	attach QPs to the group, without first joining it.

This test demonstrates how to use multicast groups with a predefined multicast GID, which is not being used by any other application.
The test consists of a server and a client - 
Both server and client joins the multicast group 
	The server - posts a SR with message "hello world" to the multicast group. using the LID it received when joining the group
	The client - attaches a QP to the multicast group, post RR and poll for completions. 
		At the end, it detaches the QP from the multicast group, in order to stop receiving messages destined 
		to it.
At the end of the test, both the server and the client leave the multicast group and destroy all the resources

If you wish to run this code example:
1. You should save the code as hello_world_mcast.c and compile it using the following command:
	gcc -I/usr -O2 hello_world_mcast.c -o hello_world_mcast -L/usr/lib64 -L/usr/lib -libverbs -libumad
	(assuming the driver was installed under /usr)
2. Make sure opensm is running
3. First run the client side, then you'll have about 10 seconds to run the server side, otherwise the client will fail.
clinet cmd for example: ./hello_world_mcast
server cmd for example: ./hello_world_mcast -s
for help run: ./hello_world_mcast -h
*/

/*
* Copyright (c) 2011 Mellanox Technologies. All rights reserved.
*
* This software is available to you under a choice of one of two
* licenses. You may choose to be licensed under the terms of the GNU
* General Public License (GPL) Version 2, available from the file
* COPYING in the main directory of this source tree, or the
* OpenIB.org BSD license below:
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
* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
* NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
* BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
* ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
* CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
* SOFTWARE.
*
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <getopt.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <infiniband/verbs.h>
/*#include <infiniband/umad.h>*/
#include <infiniband/arch.h> /* for htonll */

#define MAX_POLL_CQ_TIMEOUT 10000 /* poll CQ timeout in milisec */
#define MSG                 "hello world"
#define MSG_SIZE            (strlen(MSG) + 1)
#define GRH_SIZE            40
#define DEF_PKEY_IDX        0
#define DEF_SL              0
#define DEF_SRC_PATH_BITS   0
#define DEF_QKEY            0x11111111
#define MULTICAST_QPN       0xFFFFFF

/*** definitions section for MADs ***/
#define SUBN_ADM_ATTR_MC_MEMBER_RECORD 0x38
#define MANAGMENT_CLASS_SUBN_ADM       0x03 	  /* Subnet Administration class */
#define MCMEMBER_JOINSTATE_FULL_MEMBER 0x1
#define MAD_SIZE                       256	  /* the size of a MAD is 256 bytes */
#define QP1_WELL_KNOWN_Q_KEY           0x80010000 /* Q_Key value of QP1 */
#define DEF_TRANS_ID                   0x12345678 /* TransactionID */
#define DEF_TCLASS                     0
#define DEF_FLOW_LABLE                 0

/* generate a bit mask S bits width */
#define MASK32(S)  ( ((u_int32_t) ~0L) >> (32-(S)) )

/* generate a bit mask with bits O+S..O set (assumes 32 bit integer).
numbering bits as following:    31.........76543210 */
#define BITS32(O,S) ( MASK32(S) << (O) )

/* extract S bits from (u_int32_t)W with offset O and shifts them O places to the right 
(right justifies the field extracted).*/
#define EXTRACT32(W,O,S) ( ((W)>>(O)) & MASK32(S) )

/* insert S bits with offset O from field F into word W (u_int32_t) */
#define INSERT32(W,F,O,S) (/*(W)=*/ ( ((W) & (~BITS32(O,S)) ) | (((F) & MASK32(S))<<(O)) ))

#ifndef INSERTF
#  define INSERTF(W,O1,F,O2,S) (INSERT32(W, EXTRACT32(F, O2, S), O1, S) )
#endif

/* according to Table 187 in the IB spec 1.2.1 */
enum subn_adm_method_t {
	SUBN_ADM_METHOD_SET    = 0x2,
	SUBN_ADM_METHOD_DELETE = 0x15
};

enum subn_adm_component_mask_t {
	SUBN_ADM_COMPMASK_MGID         = (1ULL << 0),
	SUBN_ADM_COMPMASK_PORT_GID     = (1ULL << 1),
	SUBN_ADM_COMPMASK_Q_KEY	       = (1ULL << 2),
	SUBN_ADM_COMPMASK_P_KEY	       = (1ULL << 7),
	SUBN_ADM_COMPMASK_TCLASS       = (1ULL << 6),
	SUBN_ADM_COMPMASK_SL           = (1ULL << 12),
	SUBN_ADM_COMPMASK_FLOW_LABEL   = (1ULL << 13),
	SUBN_ADM_COMPMASK_JOIN_STATE   = (1ULL << 16),
};

/* according to Table 195 in the IB spec 1.2.1 */
struct sa_mad_packet_t {
	u_int8_t		mad_header_buf[24];
	u_int8_t		rmpp_header_buf[12];
	u_int64_t		SM_Key;
	u_int16_t		AttributeOffset;
	u_int16_t		Reserved1;
	u_int64_t		ComponentMask;
	u_int8_t		SubnetAdminData[200];
}__attribute__((packed));


enum mcast_state_t {
	MCAST_IS_JOINED   = 1,
	MCAST_IS_ATTACHED = (1 << 1)
};

/* structure of test parameters */
struct config_t {
	uint8_t				mcast_gid[16];	/* multicast gid */
	const char			*dev_name;	/* IB device name */
	int				ib_port;	/* local IB port to work with */
	int				is_server;	/* should be 0 if this is a client */
};

struct mcast_data_t {
	union ibv_gid 			port_gid;	/* the port's gid */
	uint16_t 			pkey;		/* the port's PKEY in index DEF_PKEY_IDX */
	uint16_t			mlid;		/* the multicast group's mlid */
	int				mcast_state;	/* indicates whether join and attach were done */ 
};

/* structure of needed test resources */
struct resources {
	struct ibv_port_attr		port_attr;	/* IB port attributes */
	struct mcast_data_t		mcast_data;	/* multicast data */
	struct ibv_device		**dev_list;	/* device list */
	struct ibv_context		*ib_ctx;	/* device handle */
	struct ibv_pd			*pd;		/* PD handle */
	struct ibv_cq			*cq;		/* CQ handle */
	struct ibv_qp			*qp;		/* QP handle */
	struct ibv_mr			*mr;		/* MR handle */
	struct ibv_ah			*ah;		/* address handle. in use only by the server, for posting SR */ 
	char				*buf;		/* memory buffer pointer */
};

/* global Variables definition */
struct config_t config = {
	{255,1,0,0,0,0,0,0,0,2,201,0,1,0,208,81},	/* mcast_gid */
	"mlx4_0",					/* dev_name */
	1,						/* ib_port */
	0						/* is_server */
};

/*****************************************
* Function: prepare_mcast_mad
*****************************************/
static void prepare_mcast_mad(
	u_int8_t method,
	struct resources *res,
	struct sa_mad_packet_t *samad_packet)
{
	u_int8_t *ptr;
	uint64_t comp_mask;
	

	/* prepare the SA packet */
	memset(samad_packet, 0, sizeof(*samad_packet));

	/* prepare the MAD header. according to Table 145 in IB spec 1.2.1 */
	ptr = samad_packet->mad_header_buf; 

	ptr[0]                     = 0x01;					/* BaseVersion */
	ptr[1]                     = MANAGMENT_CLASS_SUBN_ADM;			/* MgmtClass */
	ptr[2]                     = 0x02; 					/* ClassVersion */
	ptr[3]                     = INSERTF(ptr[3], 0, method, 0, 7); 		/* Method */
	(*(u_int64_t *)(ptr + 8))  = htonll((u_int64_t)DEF_TRANS_ID);		/* TransactionID */
	(*(u_int16_t *)(ptr + 16)) = htons(SUBN_ADM_ATTR_MC_MEMBER_RECORD);	/* AttributeID */

	/* prepare the subnet admin data. according to Table 212 in IB spec 1.2.1 */
	ptr = samad_packet->SubnetAdminData;

	memcpy(&ptr[0], config.mcast_gid, 16);		   /* MGid */
	memcpy(&ptr[16], res->mcast_data.port_gid.raw, 16);/* PortGid */

	(*(u_int32_t *)(ptr + 32)) = htonl(DEF_QKEY);						/* Q_Key */
	(*(u_int16_t *)(ptr + 40)) = htons(res->mcast_data.pkey);				/* P_Key */
	ptr[39]                    = DEF_TCLASS;						/* TClass */
	ptr[44]                    = INSERTF(ptr[44], 4, DEF_SL, 0, 4);				/* SL */
	ptr[44]                    = INSERTF(ptr[44], 0, DEF_FLOW_LABLE, 16, 4);		/* FlowLable */
	ptr[45]                    = INSERTF(ptr[45], 0, DEF_FLOW_LABLE, 8, 8);
	ptr[46]                    = INSERTF(ptr[46], 0, DEF_FLOW_LABLE, 0, 8);
	ptr[48]                    = INSERTF(ptr[48], 0, MCMEMBER_JOINSTATE_FULL_MEMBER, 0, 4);	/* JoinState */

	comp_mask = SUBN_ADM_COMPMASK_MGID | SUBN_ADM_COMPMASK_PORT_GID | SUBN_ADM_COMPMASK_Q_KEY | 
		    SUBN_ADM_COMPMASK_P_KEY | SUBN_ADM_COMPMASK_TCLASS | SUBN_ADM_COMPMASK_SL |
		    SUBN_ADM_COMPMASK_FLOW_LABEL | SUBN_ADM_COMPMASK_JOIN_STATE;

	samad_packet->ComponentMask = htonll(comp_mask);
}

/*****************************************
* Function: check_mad_status
*****************************************/
static int check_mad_status(
	struct sa_mad_packet_t *samad_packet)
{
	u_int8_t *ptr;
	u_int32_t user_trans_id;
	u_int16_t mad_header_status; 	/* only 15 bits */


	ptr = samad_packet->mad_header_buf;
	user_trans_id = ntohl(*(u_int32_t *)(ptr + 12)); /* the upper 32 bits of TransactionID were set by the kernel */

	/* check the TransactionID to make sure this is the response for the join/leave multicast group request we posted */
	if (user_trans_id != DEF_TRANS_ID) {
		fprintf(stderr, "received a mad with TransactionID 0x%x, when expecting 0x%x\n", 
			(unsigned int)user_trans_id, (unsigned int)DEF_TRANS_ID);;
		return 1;
	}

	mad_header_status = 0x0;
	mad_header_status = INSERTF(mad_header_status, 8, ptr[4], 0, 7); /* only 15 bits */
	mad_header_status = INSERTF(mad_header_status, 0, ptr[5], 0, 8);

	if (mad_header_status) {
		fprintf(stderr,"received UMAD with an error: 0x%x\n", mad_header_status);
		return 1;
	}

	return 0;
}

/*****************************************
* Function: get_mlid_from_mad
*****************************************/
static void get_mlid_from_mad(
	struct sa_mad_packet_t *samad_packet,
	uint16_t *mlid)
{
	u_int8_t *ptr;


	ptr = samad_packet->SubnetAdminData;
	*mlid = ntohs(*(u_int16_t *)(ptr + 36));
}

/*****************************************
* Function: send_mcast_msg
*****************************************/
static int send_mcast_msg(
	enum subn_adm_method_t method,
	struct resources *res)
{
	void *umad_buff = NULL;
	void *mad = NULL;
	int portid = -1;
	int agentid = -1;
	int timeout_ms, num_retries;
	int length;
	int test_result = 1;
	int rc;

	
	/* use casting to loose the "const char0 *" */
	portid = umad_open_port((char *)config.dev_name, config.ib_port);
	if (portid < 0) {
		fprintf(stderr, "failed to open UMAD port %d of device %s\n", config.ib_port, config.dev_name);
		goto cleanup;
	}
	fprintf(stdout, "UMAD port %d of device %s was opened\n", config.ib_port, config.dev_name);

	agentid = umad_register(portid, MANAGMENT_CLASS_SUBN_ADM, 2, 0, 0);
	if (agentid < 0) {
		fprintf(stderr, "failed to register UMAD agent for MADs\n");
		goto cleanup;
	}
	fprintf(stdout, "UMAD agent was registered\n");

	umad_buff = umad_alloc(1, umad_size() + MAD_SIZE);
	if (!umad_buff) {
		fprintf(stderr, "failed to allocate MAD buffer\n");
		goto cleanup;
	}

	mad = umad_get_mad(umad_buff);

	prepare_mcast_mad(method, res, (struct sa_mad_packet_t *)mad);

	rc = umad_set_addr(umad_buff, res->port_attr.sm_lid, 1, res->port_attr.sm_sl, QP1_WELL_KNOWN_Q_KEY);
	if (rc < 0) {
		fprintf(stderr, "failed to set the destination address of the SMP\n");
		goto cleanup;
	}
	timeout_ms = 100;
	num_retries = 5;

	rc = umad_send(portid, agentid, umad_buff, MAD_SIZE, timeout_ms, num_retries);
	if (rc < 0) {
		fprintf(stderr, "failed to send MAD\n");
		goto cleanup;
	}
	length = MAD_SIZE;

	rc = umad_recv(portid, umad_buff, &length, (10 * timeout_ms * num_retries));
	if (rc < 0) {
		fprintf(stderr, "failed to receive MAD response\n");
		goto cleanup;
	}

	/* make sure that the join\leave multicast group request was accepted */
	rc = check_mad_status((struct sa_mad_packet_t *)mad);
	if (rc) {
		fprintf(stderr, "failed to get mlid from MAD\n");
		goto cleanup;
	}

	/* if a "join multicast group" message was sent */
	if (method == SUBN_ADM_METHOD_SET) {
		get_mlid_from_mad((struct sa_mad_packet_t *)mad, &res->mcast_data.mlid);
		res->mcast_data.mcast_state |= MCAST_IS_JOINED;

		fprintf(stdout, "node has joined multicast group, mlid = 0x%x\n", res->mcast_data.mlid);

	} else { /* if a "leave multicast group" message was sent */
		res->mcast_data.mcast_state &= ~MCAST_IS_JOINED;

		fprintf(stdout, "node has left multicast group\n");
	}


	test_result = 0;

cleanup:
	if (umad_buff)
		umad_free(umad_buff);

	if (portid >= 0) {
		if (agentid >= 0) {
			if (umad_unregister(portid, agentid)) {
				fprintf(stderr, "failed to deregister UMAD agent for MADs\n");
				test_result = 1;
			}
		}

		if (umad_close_port(portid)) {
			fprintf(stderr, "failed to close UMAD portid\n");
			test_result = 1;
		}
	}

	return test_result;
}

/*****************************************
* Function: join_multicast_group
*****************************************/
/* get the needed information by performing queries and join a multicast group */ 
static int join_multicast_group(
	struct resources *res)
{
	int rc;


	rc = ibv_query_pkey(res->ib_ctx, config.ib_port, DEF_PKEY_IDX, &res->mcast_data.pkey);
	if (rc) {
		fprintf(stderr, "failed to query PKey table of port %d with index %d\n", config.ib_port, DEF_PKEY_IDX);
		return 1;
	}

	rc = ibv_query_gid(res->ib_ctx, config.ib_port, 0, &res->mcast_data.port_gid);
	if (rc) {
		fprintf(stderr, "failed to query GID table of port %d with index %d\n", config.ib_port, 0);
		return 1;
	}

	/* mlid will be assigned to the new LID after the join */
	if (umad_init() < 0) {
		fprintf(stderr, "failed to init the UMAD library\n");
		return 1;
	}

	/* join the multicast group */
	rc = send_mcast_msg(SUBN_ADM_METHOD_SET, res);
	if (rc) {
		fprintf(stderr, "failed to join the mcast group\n");
		return 1;
	}

	return 0;
}

/*****************************************
* Function: poll_completion
*****************************************/
static int poll_completion(
	struct resources *res)
{
	struct ibv_wc wc;
	unsigned long start_time_msec, cur_time_msec;
	struct timeval cur_time;
	int rc;


	/* poll the completion for a while before giving up of doing it .. */
	gettimeofday(&cur_time, NULL);
	start_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);

	do {
		rc = ibv_poll_cq(res->cq, 1, &wc);
		if (rc < 0) {
			fprintf(stderr, "poll CQ failed\n");
			return 1;
		}
		gettimeofday(&cur_time, NULL);
		cur_time_msec = (cur_time.tv_sec * 1000) + (cur_time.tv_usec / 1000);

	} while ((rc == 0) && ((cur_time_msec - start_time_msec) < MAX_POLL_CQ_TIMEOUT));

	/* if the CQ is empty */
	if (rc == 0) {
		fprintf(stderr, "completion wasn't found in the CQ after timeout\n");
		return 1;
	}

	fprintf(stdout, "completion was found in CQ with status 0x%x\n", wc.status);

	/* check the completion status (here we don't care about the completion opcode) */
	if (wc.status != IBV_WC_SUCCESS) {
		fprintf(stderr, "got bad completion with status: 0x%x, vendor syndrome: 0x%x\n", 
			wc.status, wc.vendor_err);
		return 1;
	}

	return 0;
}

/*****************************************
* Function: post_send
*****************************************/
static int post_send(
	struct resources *res)
{
	struct ibv_send_wr sr;
	struct ibv_sge sge;
	struct ibv_send_wr *bad_wr;
	int rc;


	/* prepare the scatter/gather entry */
	memset(&sge, 0, sizeof(sge));

	sge.addr = (uintptr_t)res->buf;
	sge.length = MSG_SIZE;
	sge.lkey = res->mr->lkey;

	/* prepare the SR */
	memset(&sr, 0, sizeof(sr));

	sr.next       = NULL;
	sr.wr_id      = 0;
	sr.sg_list    = &sge;
	sr.num_sge    = 1;
	sr.opcode     = IBV_WR_SEND;

	sr.wr.ud.ah = res->ah;
	sr.wr.ud.remote_qpn = MULTICAST_QPN;
	sr.wr.ud.remote_qkey = DEF_QKEY;

	rc = ibv_post_send(res->qp, &sr, &bad_wr);
	if (rc) {
		fprintf(stderr, "failed to post SR\n");
		return 1;
	}
	fprintf(stdout, "Send Request was posted\n");

	return 0;
}

/*****************************************
* Function: post_receive
*****************************************/
static int post_receive(
	struct resources *res)
{
	struct ibv_recv_wr rr;
	struct ibv_sge sge;
	struct ibv_recv_wr *bad_wr;
	int rc;


	/* prepare the scatter/gather entry */
	memset(&sge, 0, sizeof(sge));
	sge.addr = (uintptr_t)res->buf;
	sge.length = MSG_SIZE + GRH_SIZE;
	sge.lkey = res->mr->lkey;

	/* prepare the RR */
	memset(&rr, 0, sizeof(rr));

	rr.next = NULL;
	rr.wr_id = 0;
	rr.sg_list = &sge;
	rr.num_sge = 1;

	/* post the Receive Request to the RQ */
	rc = ibv_post_recv(res->qp, &rr, &bad_wr);
	if (rc) {
		fprintf(stderr, "failed to post RR\n");
		return 1;
	}

	fprintf(stdout, "Receive Request was posted\n");

	return 0;
}

/*****************************************
* Function: resources_init
*****************************************/
static void resources_init(
	struct resources *res)
{
	memset(res, 0, sizeof(struct resources));
}

/*****************************************
* Function: resources_create
*****************************************/
static int resources_create(
	struct resources *res)
{
	struct ibv_qp_init_attr qp_init_attr;
	struct ibv_ah_attr ah_attr;
	struct ibv_device *ib_dev = NULL;
	size_t size;
	int i;
	int mr_flags = 0;
	int cq_size = 0;
	int num_devices;


	fprintf(stdout, "searching for IB devices in host\n");

	/* get device names in the system */
	res->dev_list = ibv_get_device_list(&num_devices);
	if (!res->dev_list) {
		fprintf(stderr, "failed to get IB devices list\n");
		return 1;
	}

	/* if there isn't any IB device in host */
	if (!num_devices) {
		fprintf(stderr, "found %d device(s)\n", num_devices);
		return 1;
	}

	fprintf(stdout, "found %d device(s)\n", num_devices);

	/* search for the specific device we want to work with */
	for (i = 0; i < num_devices; i ++) {
		if (!strcmp(ibv_get_device_name(res->dev_list[i]), config.dev_name)) {
			ib_dev = res->dev_list[i];
			break;
		}
	}

	/* if the device wasn't found in host */
	if (!ib_dev) {
		fprintf(stderr, "IB device %s wasn't found\n", config.dev_name);
		return 1;
	}

	/* get device handle */
	res->ib_ctx = ibv_open_device(ib_dev);
	if (!res->ib_ctx) {
		fprintf(stderr, "failed to open device %s\n", config.dev_name);
		return 1;
	}

	/* query port properties */
	if (ibv_query_port(res->ib_ctx, config.ib_port, &res->port_attr)) {
		fprintf(stderr, "ibv_query_port on port %u failed\n", config.ib_port);
		return 1;
	}

	/* check that the port is active */
	if (res->port_attr.state != IBV_PORT_ACTIVE) {
		fprintf(stderr, "port %d is not in active state: %d\n", config.ib_port, res->port_attr.state);
		return 1;
	}

	/* allocate Protection Domain */
	res->pd = ibv_alloc_pd(res->ib_ctx);
	if (!res->pd) {
		fprintf(stderr, "ibv_alloc_pd failed\n");
		return 1;
	}

	/* each side will post only one WR, so Completion Queue with 1 entry is enough */
	cq_size = 1;
	res->cq = ibv_create_cq(res->ib_ctx, cq_size, NULL, NULL, 0);
	if (!res->cq) {
		fprintf(stderr, "failed to create CQ with %u entries\n", cq_size);
		return 1;
	}

	/* allocate the memory buffer that will hold the data. for the client we allocate extra bytes for GRH */
	size = MSG_SIZE;
	if (!config.is_server) 
		size += GRH_SIZE;

	res->buf = malloc(size);
	if (!res->buf) {
		fprintf(stderr, "failed to malloc %Zu bytes to memory buffer\n", size);
		return 1;
	}

	/* only in the server side put the message in the memory buffer */
	if (config.is_server) {
		strcpy(res->buf, MSG);
		fprintf(stdout, "going to send the message: '%s'\n", res->buf);
	} else
		memset(res->buf, 0, size);

	/* register this memory buffer */
	mr_flags = (!config.is_server) ? IBV_ACCESS_LOCAL_WRITE : 0;
	res->mr = ibv_reg_mr(res->pd, res->buf, size, mr_flags);
	if (!res->mr) {
		fprintf(stderr, "ibv_reg_mr failed with mr_flags=0x%x\n", mr_flags);
		return 1;
	}

	fprintf(stdout, "MR was registered with addr=%p, lkey=0x%x, rkey=0x%x, flags=0x%x\n",
		res->buf, res->mr->lkey, res->mr->rkey, mr_flags);

	/* create the Queue Pair */
	memset(&qp_init_attr, 0, sizeof(qp_init_attr));

	qp_init_attr.qp_type    = IBV_QPT_UD;
	qp_init_attr.sq_sig_all = 1;
	qp_init_attr.send_cq    = res->cq;
	qp_init_attr.recv_cq    = res->cq;
	qp_init_attr.cap.max_send_wr  = 1;
	qp_init_attr.cap.max_recv_wr  = 1;
	qp_init_attr.cap.max_send_sge = 1;
	qp_init_attr.cap.max_recv_sge = 1;

	res->qp = ibv_create_qp(res->pd, &qp_init_attr);
	if (!res->qp) {
		fprintf(stderr, "failed to create QP\n");
		return 1;
	}
	fprintf(stdout, "QP was created, QP number=0x%x\n", res->qp->qp_num);

	if (join_multicast_group(res)) {
		fprintf(stderr, "failed to join multicast group\n");
		return 1;
	}
	fprintf(stdout, "successfully joined a multicast group\n");

	/* create ah for the server, to be used in post SRs to the multicast group*/
	if (config.is_server) {
		memset(&ah_attr, 0 , sizeof(ah_attr));
		
		ah_attr.is_global      = 1;
		ah_attr.grh.sgid_index = 0;
		ah_attr.dlid           = res->mcast_data.mlid;
		ah_attr.sl             = DEF_SL;
		ah_attr.src_path_bits  = DEF_SRC_PATH_BITS;
		ah_attr.port_num       = config.ib_port;

		/* copy the multicast group gid */
		memcpy(ah_attr.grh.dgid.raw, config.mcast_gid, sizeof(ah_attr.grh.dgid.raw));

		res->ah = ibv_create_ah(res->pd, &ah_attr);
		if (!res->ah) {
			fprintf(stderr, "failed to create AH\n");
			return 1;
		}
		fprintf(stdout, "AH was created with dlid 0x%x\n", ah_attr.dlid);
	}

	return 0;
}

/*****************************************
* Function: modify_qp_to_init
*****************************************/
static int modify_qp_to_init(
	struct ibv_qp *qp)
{
	struct ibv_qp_attr attr;
	int flags;
	int rc;


	/* do the following QP transition: RESET -> INIT */
	memset(&attr, 0, sizeof(attr));

	attr.qp_state   = IBV_QPS_INIT;
	attr.pkey_index = 0;
	attr.port_num   = config.ib_port;
	attr.qkey       = DEF_QKEY;

	flags = IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_QKEY;

	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc) {
		fprintf(stderr, "failed to modify QP state to INIT\n");
		return rc;
	}

	return 0;
}

/*****************************************
* Function: modify_qp_to_rtr
*****************************************/
static int modify_qp_to_rtr(
	struct ibv_qp *qp)
{
	struct ibv_qp_attr attr;
	int flags;
	int rc;


	/* do the following QP transition: INIT -> RTR */
	memset(&attr, 0, sizeof(attr));

	attr.qp_state = IBV_QPS_RTR;

	flags = IBV_QP_STATE;

	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc) {
		fprintf(stderr, "failed to modify QP state to RTR\n");
		return rc;
	}

	return 0;
}

/*****************************************
* Function: modify_qp_to_rts
*****************************************/
static int modify_qp_to_rts(
	struct ibv_qp *qp)
{
	struct ibv_qp_attr attr;
	int flags;
	int rc;


	/* do the following QP transition: RTR -> RTS */
	memset(&attr, 0, sizeof(attr));

	attr.qp_state = IBV_QPS_RTS;
	attr.sq_psn   = 0;

	flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

	rc = ibv_modify_qp(qp, &attr, flags);
	if (rc) {
		fprintf(stderr, "failed to modify QP state to RTS\n");
		return rc;
	}

	return 0;
}

/*****************************************
* Function: prepare_qps_for_post
*****************************************/
static int prepare_qps_for_post(
	struct resources *res)
{
	union ibv_gid gid;
	int rc;

	
	/* modify the QP to init */
	rc = modify_qp_to_init(res->qp);
	if (rc) {
		fprintf(stderr, "failed to modify QP state from RESET to INIT\n");
		return rc;
	}

	/* modify the QP to RTR */
	rc = modify_qp_to_rtr(res->qp);
	if (rc) {
		fprintf(stderr, "failed to modify QP state from INIT to RTR\n");
		return rc;
	}

	/* only the server post SR, so only he should be in RTS
	   (the client can be moved to RTS as well)
	 */
	if (!config.is_server)
		fprintf(stdout, "QP state was change to RTR\n");
	else {
		rc = modify_qp_to_rts(res->qp);
		if (rc) {
			fprintf(stderr, "failed to modify QP state from RTR to RTS\n");
			return rc;
		}

		fprintf(stdout, "QP state was change to RTS\n");
	}

	/* attach the client's QP to the multicast group 
	   so it'll receive incoming messages destined to the group */
	if (!config.is_server) {
		memcpy(gid.raw, config.mcast_gid, sizeof(gid.raw)); 

		rc = ibv_attach_mcast(res->qp, &gid, res->mcast_data.mlid);
		if (rc) {
			fprintf(stderr, "failed to Attach QP to multicast group with mlid 0x%x\n", res->mcast_data.mlid);
			return 1;
		}
		res->mcast_data.mcast_state |= MCAST_IS_ATTACHED;

		fprintf(stdout, "QP was Attached to multicast group with mlid 0x%x\n", res->mcast_data.mlid);
	}

	return 0;
}

/*****************************************
* Function: mcast_res_destroy
*****************************************/
static int mcast_res_destroy(
	struct resources *res)
{
	union ibv_gid gid;
	int rc;
	int test_result = 0;


	/* if the QP was attached to a multicast group - detach it */
	if (res->mcast_data.mcast_state & MCAST_IS_ATTACHED) {
		memset(&gid,0,sizeof(union ibv_gid));
		memcpy(gid.raw, config.mcast_gid, sizeof(gid.raw));

		rc = ibv_detach_mcast(res->qp, &gid, res->mcast_data.mlid);
		if (rc) {
			fprintf(stderr, "failed to Detach QP from multicast group with lid 0x%x\n", res->mcast_data.mlid);
			test_result = 1;
		}

		res->mcast_data.mcast_state &= ~MCAST_IS_ATTACHED;
	}

	/* leave the multicast group */
	if (res->mcast_data.mcast_state & MCAST_IS_JOINED) {
		rc = send_mcast_msg(SUBN_ADM_METHOD_DELETE, res);
		if (rc) {
			fprintf(stderr, "failed to leave the mcast group\n");
			test_result = 1;
		}
	}

	return test_result;
}

/*****************************************
* Function: resources_destroy
*****************************************/
static int resources_destroy(
	struct resources *res)
{
	int test_result = 0;


	if (mcast_res_destroy(res)) {
		fprintf(stderr, "failed to destroy multicast resources\n");
		test_result = 1;
	}

	if (res->ah) {
		if (ibv_destroy_ah(res->ah)) {
			fprintf(stderr, "failed to destroy AH\n");
			test_result = 1;
		}
	}

	if (res->qp) {
		if (ibv_destroy_qp(res->qp)) {
			fprintf(stderr, "failed to destroy QP\n");
			test_result = 1;
		}
	}

	if (res->mr) {
		if (ibv_dereg_mr(res->mr)) {
			fprintf(stderr, "failed to deregister MR\n");
			test_result = 1;
		}
	}

	if (res->buf)
		free(res->buf);

	if (res->cq) {
		if (ibv_destroy_cq(res->cq)) {
			fprintf(stderr, "failed to destroy CQ\n");
			test_result = 1;
		}
	}

	if (res->pd) {
		if (ibv_dealloc_pd(res->pd)) {
			fprintf(stderr, "failed to deallocate PD\n");
			test_result = 1;
		}
	}

	if (res->ib_ctx) {
		if (ibv_close_device(res->ib_ctx)) {
			fprintf(stderr, "failed to close device context\n");
			test_result = 1;
		}
	}

	if (res->dev_list)
		ibv_free_device_list(res->dev_list);


	return test_result;
}

/*****************************************
* Function: print_config
*****************************************/
static void print_config(void)
{
	fprintf(stdout, " ------------------------------------------------------------------------\n");
	fprintf(stdout, " Device name                  : \"%s\"\n", config.dev_name);
	fprintf(stdout, " IB port                      : %u\n", config.ib_port);
	fprintf(stdout, " Is server?                   : %s\n", (config.is_server ? "Yes" : "No"));
	fprintf(stdout, " Multicast GID                : %u.%u.%u.%u.%u.%u.%u.%u.%u.%u.%u.%u.%u.%u.%u.%u\n",
		config.mcast_gid[0], config.mcast_gid[1], config.mcast_gid[2], config.mcast_gid[3],
		config.mcast_gid[4], config.mcast_gid[5], config.mcast_gid[6], config.mcast_gid[7],
		config.mcast_gid[8], config.mcast_gid[9], config.mcast_gid[10], config.mcast_gid[11],
		config.mcast_gid[12], config.mcast_gid[13], config.mcast_gid[14], config.mcast_gid[15]);
	fprintf(stdout, " ------------------------------------------------------------------------\n\n");
}

/*****************************************
* Function: str2gid
******************************************/
/* Convert gid from str to array[16] */
int str2gid(
	const char *gid_str,
	uint8_t *gid)
{
	const char *cur_char;
	char *next_char;
	int i;


	for (i = 0, cur_char = gid_str; i < 16; i++) { 
		*(gid + i) = (uint8_t)strtoul(cur_char, &next_char , 10);

		if ((next_char == cur_char) || ((i < 15) && (*next_char != '.'))) {
			fprintf(stderr, "decimal dotted format must be used for DGID, e.g. 10.0.0.229.... (16 numbers)");
			return 1;
		}
		cur_char= next_char+1;  /* skip dot */
	}

	return 0;
}

/*****************************************
* Function: usage
*****************************************/
static void usage(const char *argv0)
{
	fprintf(stdout, "Usage:\n");
	fprintf(stdout, "  %s -s         join the multicast group and post SR\n", argv0);
	fprintf(stdout, "  %s            join the multicast group, attach a QP to it and post RR\n", argv0);
	fprintf(stdout, "\n");
	fprintf(stdout, "Options:\n");
	fprintf(stdout, "  -s, --server           this is the server (default NO)\n");
	fprintf(stdout, "  -d, --ib-dev=<dev>     use IB device <dev> (default mlx4_0)\n");
	fprintf(stdout, "  -i, --ib-port=<port>   use port <port> of IB device (default 1)\n");
	fprintf(stdout, "  -g, --mcast_gid=<gid>  multicast GID\n");
}

/*****************************************
******************************************
* Function: main
******************************************
*****************************************/
int main(int argc, char *argv[])
{
	struct resources res;
	int test_result = 1;


	/* parse the command line parameters */
	while (1) {
		int c;

		static struct option long_options[] = {
			{ .name = "server",    .has_arg = 0, .val = 's' },
			{ .name = "ib-dev",    .has_arg = 1, .val = 'd' },
			{ .name = "ib-port",   .has_arg = 1, .val = 'i' },
			{ .name = "mcast-gid", .has_arg = 1, .val = 'g' },
			{ .name = NULL,        .has_arg = 0, .val = '\0'}
		};

		c = getopt_long(argc, argv, "sd:i:g", long_options, NULL);
		if (c == -1)
			break;

		switch (c) {
		case 's':
			config.is_server = 1;
			break;
		case 'd':
			config.dev_name = optarg;
			break;

		case 'i':
			config.ib_port = strtoul(optarg, NULL, 0);
			if (config.ib_port < 0) {
				usage(argv[0]);
				return 1;
			}
			break;

		case 'g':
			if (str2gid(optarg, config.mcast_gid))
				return 1;

		default:
			usage(argv[0]);
			return 1;
		}
	}

	/* print the used parameters for info*/
	print_config();

	/* init all of the resources, so cleanup will be easy */
	resources_init(&res);

	/* create resources before using them */
	if (resources_create(&res)) {
		fprintf(stderr, "failed to create resources\n");
		goto cleanup;
	}

	/* prepare the QPs for posting WRs */
	if (prepare_qps_for_post(&res)) {
		fprintf(stderr, "failed to prepare QPs for posting WRs\n");
		goto cleanup;
	}

	/* let the server post the SR */
	if (config.is_server) {
		sleep(1); /* sleep to make sure the client already attached it's QP to the multicast group */

		if (post_send(&res)) {
			fprintf(stderr, "failed to post SR\n");
			goto cleanup;
		}
	} else { /* let the client post RR to be prepared for incoming messages */

		if (post_receive(&res)) {
			fprintf(stderr, "failed to post RR\n");
			goto cleanup;
		}
	}

	/* in both sides we expect to get a completion */
	if (poll_completion(&res)) {
		 fprintf(stderr, "poll completion failed\n");
		 goto cleanup;
	}

	/* after polling the completion we have the message in the client buffer too */
	if (!config.is_server)
		printf("Message is '%s'\n", (res.buf + GRH_SIZE));

	test_result = 0;

cleanup:
	if (resources_destroy(&res)) {
		fprintf(stderr, "failed to destroy resources\n");
		test_result = 1;
	}

	if (test_result)
		fprintf(stdout, "\nprogram failed\n");
	else
		fprintf(stdout, "\nprogram ended successfully\n");

	return test_result;
}
