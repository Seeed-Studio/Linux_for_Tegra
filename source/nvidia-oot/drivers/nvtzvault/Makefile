# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.

# Main driver that depends on SOC Plugin kernel module
nvtzvault-objs += nvtzvault-main.o nvtzvault-helper.o
nvtzvault-y += nvtzvault-main.o
obj-m += nvtzvault.o

# SOC Plugin kernel module
nvtzvault-soc-comms-objs += teec-soc-plugin.o teec-soc-mailbox.o teec-soc-ivc.o
nvtzvault-soc-comms-y += teec-soc-plugin.o
obj-m += nvtzvault-soc-comms.o
