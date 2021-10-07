.PHONY: clean

# CFLAGS  := -Wall -Werror -g
# CC      := gcc
LDLIBS  := ${LDLIBS} -lrdmacm -libverbs -lpthread

CXX := g++
CXXFLAGS  := -Wall -Werror -g

APPS    := client server tdma-server

all: ${APPS}

client: common.o client.o
	${CXX} -o $@ $^ ${LDLIBS}

server: common.o server.o
	${CXX} -o $@ $^ ${LDLIBS}

# tdma-server.o: common.o server_tdma.cpp
# 	${CXX} ${CXXFLAGS} -c -o $@ $^

tdma-server: common.o tdma-server.o
	${CXX} -o $@ $^ ${LDLIBS}
# ${CXX} -o $@ $^ ${LDLIBS}

clean:
	rm -f *.o ${APPS}

