# Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software and related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

import importlib

from bootburn_lib import bootburn_lib
from flashtools_nverror import nverror
from flashtools_nverror import AbnormalTermination


class yaml_config:
    yaml_data = dict()
    yml = None
    def __init__(self, yaml_file, lib):
        try:
            self.targetConfig = lib.targetConfig
            self.bootburnLib = lib
            self.yml = importlib.import_module('yaml')

            with open(yaml_file) as cfile:
                yaml_config.yaml_data = self.yml.safe_load(cfile)
            self.targetConfig.sysMonitor.log("Found " + yaml_file + " platform config\n", False)
        except OSError:
            self.targetConfig.sysMonitor.log("File " + yaml_file + " not found\n", True)
            AbnormalTermination("Failed to import config", nverror.NvError_ResourceError)

    def update(self, yaml_file, build_data):
        try:
            with open(yaml_file, "w+") as cfile:
                self.yml.dump(build_data, cfile, default_flow_style=False)
        except OSError:
            self.targetConfig.sysMonitor.log("File " + yaml_file + " not found\n", True)
            AbnormalTermination("Failed to update config", nverror.NvError_ResourceError)


