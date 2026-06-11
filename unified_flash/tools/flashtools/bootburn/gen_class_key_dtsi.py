#!/usr/bin/python3
#
# SPDX-FileCopyrightText: Copyright (c) 2023-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA Corporation and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA Corporation is strictly prohibited.
#

import sys
import os
import importlib

from select_socgrp import select_socgrp

if __name__ == "__main__":
    print("****** Starting bootburn/gen_class_key_dtsi.py ********")

    socgrpSelect = select_socgrp()
    socgrpSelect.isSocGrpFound()

    if socgrpSelect.isSocGrpFound():
        gen_class_key_module = importlib.import_module(f"{socgrpSelect.soc_scripts_dir}.gen_class_key_dtsi")
        gen_class_key_func = getattr(gen_class_key_module, "main")
        result = gen_class_key_func(sys.argv[1:])
        #gen_class_key_dtsi.main(sys.argv[1:])
    else:
        print(f"ERROR: Board {sys.argv[sys.argv.index('-b') + 1]} not a supported board")
        print("provide a supported board name with -b option")
        sys.exit(1)
