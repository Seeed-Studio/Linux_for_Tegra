#!/bin/bash
set -e

kernel_version="`modinfo -Fvermagic ./mst_backward_compatibility/mst_ppc/mst_ppc_pci_reset.ko | awk '{ print $1 }'`"
path_to_build="`pwd`"
path_to_build="$path_to_build/../build"
cd $path_to_build

mkdir -p /etc/mft/mlxfwreset/$kernel_version
/bin/cp -f ./mst_backward_compatibility/mst_ppc/mst_ppc_pci_reset.ko /etc/mft/mlxfwreset/$kernel_version/

cd -

