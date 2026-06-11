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

class nv_base_thor_target(nv_base_topo_target):
    """NV_BASE_THOR_TARGET"""
    PCA9539_P02 = 2     # bit 2 of a 16-bit register, indicating output "P02"  ((listed P0..P7, P10..P17)
    PCA9539_P03 = 3     # bit 3 of a 16-bit register, indicating output "P03"  ((listed P0..P7, P10..P17)
    SOCKET_ID = 1 << PCA9539_P02    # output P02 of pca9539
    FT_TEG_RST = 1 << PCA9539_P03   # output P03 of pca9539
    T2NOTT1 = FT_TEG_RST            # as repurposed for some SKUs
    PCA9539_SOCKETID_MASK = SOCKET_ID
    PCA9539_T2NOTT1_MASK = T2NOTT1  # repurposed on Thor for T1/T2 differentiation, for some SKUs

    @staticmethod
    def _thor_aspect_i2c_adjust(aspect):
        print("Adjusting I2C frequency on %s" % aspect.name)
        i2c_config = aspect.get_I2C_config()
        orig_freq = i2c_config.i2c_frequency_khz
        i2c_config.i2c_frequency_khz = 100 if orig_freq == 400 else 400
        aspect.set_I2C_config(i2c_config)

    def __init__(self, topos):
        nv_base_topo_target.__init__(self, topos)

    def __enter__(self):
        super(nv_base_thor_target, self).__enter__()  # have to use python 2 syntax here, for now
        for aspect in self.aspects:
            nv_base_thor_target._thor_aspect_i2c_adjust(aspect)
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        return super(nv_base_thor_target, self).__exit__(exc_type, exc_val, exc_tb)  # have to use python 2 syntax here, for now

    def recovery_status(self):
        return super(nv_base_thor_target, self).recovery_status(pid=0x7026)  # have to use python 2 syntax here, for now

