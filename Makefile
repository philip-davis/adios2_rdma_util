FABRIC_CFLAGS=$(shell pkg-config --cflags libfabric)
FABRIC_LDFLAGS=$(shell pkg-config --libs libfabric)
CRAYDRC_CFLAGS=$(shell pkg-config --cflags cray-drc 2>/dev/null)
CRAYDRC_LDFLAGS=$(shell pkg-config --libs cray-drc 2>/dev/null)
FI_GNI_H=$(shell pkg-config --variable=includedir libfabric)/rdma/fi_ext_gni.h


ifneq ("$(wildcard $(FI_GNI_H))","")
	CRAY_CFLAGS=-DSST_HAVE_FI_GNI
endif

ifneq ("$(CRAYDRC_CFLAGS)", "")
	CRAY_CFLAGS=-DSST_HAVE_CRAY_DRC $(CRAY_CFLAGS) $(CRAYDRC_CFLAGS)
	CRAY_LDFLAGS=$(CRAYDRC_LDFLAGS)
endif

all: adios2_rdma_util

adios2_rdma_util: adios2_rdma_util.o
	$(CC) -o adios2_rdma_util adios2_rdma_util.o $(FABRIC_LDFLAGS) $(CRAY_LDFLAGS)


adios2_rdma_util.o: adios2_rdma_util.c
	$(CC) -g -c adios2_rdma_util.c $(CRAY_CFLAGS) $(FABRIC_CFLAGS)

clean:
	rm -f adios2_rdma_util adios2_rdma_util.o
