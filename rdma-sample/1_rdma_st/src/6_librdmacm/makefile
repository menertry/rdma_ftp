all: client server xclient xserver

client: rdma_client.c
	gcc rdma_client.c -o rdma_client -g -Wall -libverbs -lrdmacm

server: rdma_server.c
	gcc rdma_server.c -o rdma_server -g -Wall -libverbs -lrdmacm

xclient: rdma_xclient.c
	gcc rdma_xclient.c -o rdma_xclient -g -Wall -libverbs -lrdmacm

xserver: rdma_xserver.c
	gcc rdma_xserver.c -o rdma_xserver -g -Wall -libverbs -lrdmacm
