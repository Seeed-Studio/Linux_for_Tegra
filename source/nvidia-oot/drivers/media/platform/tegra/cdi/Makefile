# SPDX-FileCopyrightText: Copyright (c) 2022-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: GPL-2.0-only
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
# more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <http://www.gnu.org/licenses/>.

obj-m	+= cdi_gpio.o
obj-m	+= cdi_mgr.o
obj-m	+= cdi_dev.o
ifneq ($(NV_BUILD_SYSTEM_TYPE),embedded-linux)
obj-m	+= cdi_pwm.o
ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
obj-m   += cam_cdi_tsc.o
endif
endif
