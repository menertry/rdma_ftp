all: client server

client: rdma_client.c
	gcc rdma_client.c -o rdma_client -g -Wall -libverbs -lrdmacm

server: rdma_server.c
	gcc rdma_server.c -o rdma_server -g -Wall -libverbs -lrdmacm
