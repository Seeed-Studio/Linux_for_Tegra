#!/usr/bin/python3
#####################################################################
# SPDX-FileCopyrightText: Copyright (c) 2024-2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.
#####################################################################

import yaml
import sys
import os
import importlib

from select_socgrp import select_socgrp

#USAGE  '-h' only option is  defaultrouted to  t23x handling
#       '-h' option along with '-b'option is supported on all  SOCs'
#            ex: -b <board-name> --h

if __name__ == "__main__":
    print("********* Starting bootburn/create_bsp_images.py *********")
    print("Command Line Arguments :: " + str(sys.argv))

    socgrpSelect = select_socgrp()

    if socgrpSelect.isSocGrpFound():
        print("********* Starting bootburn/create_bsp_images.py *********")
        create_bsp_module = importlib.import_module(f"{socgrpSelect.soc_scripts_dir}.create_bsp_images")
        create_bsp_func = getattr(create_bsp_module, "create_bsp")
        result = create_bsp_func(sys.argv)
        sys.exit(result)
    else:
        print(f"ERROR: Board {sys.argv[sys.argv.index('-b') + 1]} not a supported board")
        print("provide a supported board name with -b option")
        sys.exit(1)
