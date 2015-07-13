.PHONY: clean

CFLAGS  := -Wall -Werror -g
LD      := gcc
LDFLAGS := ${LDFLAGS} -libverbs -libumad

APPS    := hello_world_mcast

all: ${APPS}

hello_world_mcast: hello_world_mcast.o
	${LD} -o $@ $^ ${LDFLAGS}

clean:
	rm -f *.o ${APPS}

