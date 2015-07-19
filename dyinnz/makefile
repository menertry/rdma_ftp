CFLAGS 	:= -Wall -g
LD 		:= gcc
LDFLAGS := ${LDFLAGS} -lrdmacm -libverbs -lpthread -levent -L../libevent

main: server.c
	gcc server.c -o server ${CFLAGS} ${LDFLAGS}

