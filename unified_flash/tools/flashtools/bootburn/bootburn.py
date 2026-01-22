#!/usr/bin/python3
####################################################################
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

#USAGE  '-h' only option and  'find_board_name' option'y
#            default routed to  t23x
#       '-h' option along with '-b'option is supported on all  SOCs'
#            ex: -b <board_name> -h
#       '--find_board_name' is supported only on t23x
#FIXME
#     Implement SOC data reader from Aurix info and call
#      corresponding SOC stack for '-h' and '--find_board_name'
#      options

if __name__ == "__main__":
    print("********* Starting bootburn/bootburn.py *********")
    print("Command Line Arguments :: " + str(sys.argv))

    socgrpSelect = select_socgrp()

    if socgrpSelect.isSocGrpFound():
        bootburn_module = importlib.import_module(f"{socgrpSelect.soc_scripts_dir}.bootburn")
        bootburn_func = getattr(bootburn_module, "bootburn")
        result = bootburn_func(sys.argv)
        sys.exit(result)
    else:
        print(f"ERROR: Board {sys.argv[sys.argv.index('-b') + 1]} not a supported board")
        print("provide a supported board name with -b option")
        sys.exit(1)
