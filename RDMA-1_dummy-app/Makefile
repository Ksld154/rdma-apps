.PHONY: clean

CFLAGS  := -Wall -Werror -g
LD      := g++
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := client server

all: ${APPS}

client: common.o client.o
	${LD} -o $@ $^ ${LDLIBS}

server: common.o server.o dummy_app.o
	${LD} -o $@ $^ ${LDLIBS}

# add_one.o : add_one.cpp
# 	${LD} ${CFLAGS} -MMD -c $< -o $@

clean:
	rm -f *.o ${APPS}

