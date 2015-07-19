CFLAGS 	:= -Wall -g
LD 		:= gcc
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -I../libevent/include ../libevent/lib/libevent.a

main: server.c
	gcc server.c -o server ${CFLAGS} ${LDFLAGS}

