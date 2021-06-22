FABRIC_CFLAGS=$(shell pkg-config --cflags libfabric)
FABRIC_LDFLAGS=$(shell pkg-config --libs libfabric)

all: adios2_rdma_util

adios2_rdma_util: adios2_rdma_util.o
	$(CC) -o adios2_rdma_util adios2_rdma_util.o $(FABRIC_LDFLAGS)


adios2_rdma_util.o: adios2_rdma_util.c
	$(CC) -g -c adios2_rdma_util.c $(FABRIC_CFLAGS)

clean:
	rm -f adios2_rdma_util adios2_rdma_util.o
