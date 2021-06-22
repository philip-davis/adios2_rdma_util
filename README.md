# adios2_rdma_util

Utility for testing the viability of RDMA capabilities for the [ADIOS2](https://github.com/ornladios/ADIOS2) data management library. This utility performs the same, configuration, initialization and finalize steps that the RDMA dataplane of SST performs without needing to run an ADIOS2-enabled application.

## building

The utility requires MPI and LibFabric. The MPI C compiler should be found in the `CC` environment variable. LibFabric must be findable with `pkg-config` (i.e. a directory including `libfabric.pc` should be included in `PKG_CONFIG_PATH`).

To build, simply run `make` in the repo directory.

## running

The utility takes no arguments. On an HPC machine, it should be run with an MPI-enabled runner, in order to be sure it is running on the compute nodes.
