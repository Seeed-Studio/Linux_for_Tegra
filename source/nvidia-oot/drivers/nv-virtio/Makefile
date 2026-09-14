# Copyright (c) 2022-2023, NVIDIA CORPORATION.  All rights reserved.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.

# Nvidia Virtio console PoC driver.

ifeq ($(shell expr $(VERSION) \> 4), 1)
ccflags-y += -Werror
EXTRA_CFLAGS += -I$(srctree.nvidia-oot)/drivers/virt/tegra/
obj-m := nv-virtio-console-poc.o
endif
