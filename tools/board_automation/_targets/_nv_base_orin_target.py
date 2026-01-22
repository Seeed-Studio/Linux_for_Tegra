#! /usr/bin/env python3

# SPDX-FileCopyrightText: Copyright (c) 2021-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

from ._nv_base_topo_target import *


class nv_base_orin_target(nv_base_topo_target):
    """NV_BASE_ORIN_TARGET"""
    # From board team:
    # measurement shows 2.4s until VDD_5V0_CVM drops below 500mV, so 3
    # seconds delay would be safe including CVM reset. However the delay could
    # be different on another board, so making the delay configurable.
    def __init__(self, topo, off_delay=3):
        super(nv_base_orin_target, self).__init__(topo)  # have to use python 2 syntax here, for now
        self.off_delay = off_delay

    def recovery_status(self):
        return super(nv_base_orin_target, self).recovery_status(pid=0x7023)  # have to use python 2 syntax here, for now

    def target_power_off(self, aid=None):
        # On SLT, the power on button will actually toggle the power on and off.
        # So the board might be in unknown state if we do not keep track the
        # power on history.
        print("Powering off the board.")
        self.push_button("FORCE_OFF", delay=self.off_delay)
        print("Powering off done.")


