# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

GCOV_PROFILE := y
ccflags-y += -Werror

obj-m := nvadsp.o

nvadsp-objs += dev.o os.o app.o app_loader_linker.o\
	 amc.o nvadsp_shared_sema.o \
	 hwmailbox.o mailbox.o msgq.o \
	 mem_manager.o aram_manager.o \
	 acast.o adsp_console_dbfs.o

# T234/T239/T194/T186
nvadsp-objs += dev-t18x.o os-t18x.o

# T264
nvadsp-objs += dev-t264.o

# T264 AON
nvadsp-objs += dev-t264-aon.o
