# SPDX-FileCopyrightText: Copyright (c) 2023-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

# Free-standing Tegra Camera Kernel Tests
ifneq ($(NV_BUILD_SYSTEM_TYPE),embedded-linux)
sensor_kernel_tests-m += sensor_dt_test.o
sensor_kernel_tests-m += sensor_dt_test_nodes.o

# Tegra Camera Kernel Tests Utilities
obj-m += utils/tegracam_log.o

# Sensor Kernel Tests Module
obj-m += sensor_kernel_tests.o
sensor_kernel_tests-m += modules/sensor_kernel_tests_core.o
sensor_kernel_tests-m += modules/sensor_kernel_tests_runner.o
endif