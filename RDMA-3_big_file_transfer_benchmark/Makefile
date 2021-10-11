.PHONY: clean

CXX := g++
CXXFLAGS  := -Wall -Werror -g

LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

APPS    := client server tdma-client

all: ${APPS}

client: common.o client.o
	${CXX} -o $@ $^ ${LDLIBS}

tdma-client: tdma-common.o tdma-client.o
	${CXX} -o $@ $^ ${LDLIBS}

server: common.o server.o
	${CXX} -o $@ $^ ${LDLIBS}
clean:
	rm -f *.o ${APPS}

