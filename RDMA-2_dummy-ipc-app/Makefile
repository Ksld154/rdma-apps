.PHONY: clean

CFLAGS  := -Wall -Werror -g
LD      := g++
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := client server sock-server

all: ${APPS}

client: common.o client.o
	${LD} -o $@ $^ ${LDLIBS}

server: common.o server.o dummy_app.o sock_client.o
	${LD} -o $@ $^ ${LDLIBS}

sock-server: sock_server.o
	g++ -o $@ $^ 

sock-client: sock_client.o
	g++ -o $@ $^ 

clean:
	rm -f *.o ${APPS}

