#!/usr/bin/python3
####################################################################
# SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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
from typing import Dict, List, Optional
import logging

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(levelname)s - %(message)s'
)
logger = logging.getLogger(__name__)

# Constants
DEFAULT_THOR_BOARD = "p3960-10-sw01"  # Default for t264
DEFAULT_ORIN_BOARD = "p3710-10-a01"   # Default for t23x
DEFAULT_BOARD = "p3960-10-sw01"       # Default for t23x

#USAGE  '-h' only option and  'find_board_name' option'y
#            default routed to  t23x
#       '-h' option along with '-b'option is supported on all  SOCs'
#            ex: -b <board_name> -h
#       '--find_board_name' is supported only on t23x
#FIXME
#     Implement SOC data reader from Aurix info and call
#      corresponding SOC stack for '-h' and '--find_board_name'
#      options

class select_socgrp:
    """
    A class to handle SOC group selection based on board names.

    This class reads from a YAML mapping file to determine the appropriate
    SOC scripts directory based on the provided board name.
    """

    def __init__(self) -> None:
        """Initialize the select_socgrp class."""
        logger.info("Initializing SOC group selector")
        self.socGrpFound: bool = False
        self.soc_scripts_dir: str = ""

    def _get_boardname_to_soc_mapping_file(self, script_dir: str, is_pdk_package: bool) -> str:
        """Get the path to the boardname to SOC mapping file."""
        if is_pdk_package:
            return os.path.join(script_dir, "..", "board_configs", "boardname_to_soc_mappings.yaml")
        return os.path.join(script_dir, "..", "..", "board_configs", "boardname_to_soc_mappings.yaml")

    def _get_default_board_name(self) -> str:
        """Determine the default board name based on command line arguments."""
        if '--find_board_name' in sys.argv:
            if 'thor' in sys.argv:
                return DEFAULT_THOR_BOARD
            elif 'orin' in sys.argv:
                return DEFAULT_ORIN_BOARD
        return DEFAULT_BOARD

    def _load_mappings(self, mapping_file: str) -> Dict:
        """Load and parse the YAML mapping file."""
        try:
            with open(mapping_file, "r") as boardname_to_soc_mapping:
                return yaml.safe_load(boardname_to_soc_mapping)
        except yaml.YAMLError as exc:
            logger.error(f"Unable to load file {mapping_file}: {exc}")
            sys.exit(1)
        except FileNotFoundError:
            logger.error(f"Mapping file not found: {mapping_file}")
            sys.exit(1)

    def isSocGrpFound(self) -> bool:
        """
        Determine if a SOC group was found based on the board name.

        Returns:
            bool: True if a SOC group was found, False otherwise
        """
        script_dir = os.path.dirname(os.path.abspath(__file__))
        parent_dir = os.path.join(script_dir, os.pardir)
        sys.path.append(parent_dir)

        flash_path = os.path.join(script_dir, "..", "..", "..", "tools", "flashtools")
        is_pdk_package = os.path.isdir(flash_path)

        mapping_file = self._get_boardname_to_soc_mapping_file(script_dir, is_pdk_package)
        mappings = self._load_mappings(mapping_file)

        # Get board name from command line arguments or use default
        board_name = ""
        if '-b' in sys.argv:
            try:
                board_name = sys.argv[sys.argv.index('-b') + 1]
            except IndexError:
                logger.error("Board name not provided after -b option")
                sys.exit(1)
        else:
            board_name = self._get_default_board_name()

        # Find matching SOC group
        for board_names, paths_dict in mappings.items():
            boards = board_names.split(" ")
            if any(socgrp in board_name for socgrp in boards):
                self.socGrpFound = True
                self.soc_scripts_dir = paths_dict["pdk_dir_name" if is_pdk_package else "int_dir_name"]
                sys.path.insert(0, os.path.join(parent_dir, self.soc_scripts_dir))
                logger.info(f"Found SOC group for board {board_name}")
                break

        if not self.socGrpFound:
            logger.warning(f"No SOC group found for board {board_name}")

        return self.socGrpFound
