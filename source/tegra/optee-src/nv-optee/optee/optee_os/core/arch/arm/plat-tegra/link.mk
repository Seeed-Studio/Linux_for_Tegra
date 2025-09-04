# SPDX-License-Identifier: BSD-2-Clause
#
# Copyright (c) 2020-2021, NVIDIA CORPORATION. All rights reserved.
#

include core/arch/arm/kernel/link.mk

all: $(link-out-dir)/tee-raw.bin

cleanfiles += $(link-out-dir)/tee-raw.bin
$(link-out-dir)/tee-raw.bin: $(link-out-dir)/tee.elf scripts/gen_tee_bin.py
	@$(cmd-echo-silent) '  GEN     $@'
	$(q)$(PYTHON3) scripts/gen_tee_bin.py --input $< --out_tee_raw_bin $@
