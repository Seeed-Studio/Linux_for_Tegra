# Copyright (c) 2019, NVIDIA CORPORATION. All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software and related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

import json
from bootburn_lib import bootburn_lib
from flashtools_nverror import nverror
from flashtools_nverror import AbnormalTermination



class json_config:
    config_data = dict()

    def __init__(self, config_file, lib):
        try:
            self.targetConfig = lib.targetConfig
            self.bootburnLib = lib

            with open(config_file) as cfile:
                json_config.config_data = json.load(cfile)
            self.targetConfig.sysMonitor.log("json data " + str(self.config_data) + "\n", False)
            self.targetConfig.sysMonitor.log("Found " + config_file + " json config\n", False)

        except OSError:
            self.targetConfig.sysMonitor.log("File " + config_file + " not found\n", True)
            AbnormalTermination("Failed to import json config", nverror.NvError_ResourceError)


    def update(self, config_file, build_data):
        try:
            with open(config_file, "w+") as cfile:
                cfile.write(json.dumps(build_data))
        except OSError:
            self.targetConfig.sysMonitor.log("File " + config_file + " not found\n", True)
            AbnormalTermination("Failed to update json config", nverror.NvError_ResourceError)


