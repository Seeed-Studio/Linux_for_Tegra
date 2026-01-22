#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2017-2025, NVIDIA CORPORATION.  All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary

#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

#####
import os
import sys
import bootburn_lib
import traceback
import time
import io
print_capture = io.StringIO()
sys.stdout = print_capture
from target_config import target_config, CustomerData
from bootburn_parser import commandline_parser
from flashtools_nverror import nverror
from flashtools_nverror import AbnormalTermination
from flash_utilities import shell_utilities
from system_monitor import monitor
import shutil
import json

sys.stdout = sys.__stdout__

def create_bsp_active(bootburnLib, targetConfig, getSkuFromTarget=False):
    try:
        start_time = time.time()
        targetConfig.sysMonitor.log("Beginning create BSP\n", True)

        shellUtils = shell_utilities()

        if(targetConfig.p_OutDirPath == None):
            AbnormalTermination("Must specify an output directory with -g for create BSP", nverror.NvError_FileOperationFailed)

        targetConfig.sysMonitor.log("output path = " + targetConfig.p_OutDirPath, True)
        if os.path.exists(targetConfig.p_OutDirPath):
            shutil.rmtree(targetConfig.p_OutDirPath)
        try:
            os.makedirs(targetConfig.p_OutDirPath)
        except Exception as e:
            targetConfig.sysMonitor.log(str(e), True)
            AbnormalTermination("failed to create dir " + targetConfig.p_OutDirPath, nverror.NvError_FileOperationFailed)

        n_RcmMode = targetConfig.parsed_commandline["R"]
        bootburnLib.HostSpaceCheck()
        bootburnLib.CopyTegraFlashOfflineUtilities(targetConfig.p_OutDirPath)
        if not(targetConfig.parsed_commandline["l4t"] or targetConfig.parsed_commandline["vdk"]) and (targetConfig.TEST_AUTOMATION == False):

            from bootburn_firmware import MCU_Firmware
            mcufw = MCU_Firmware(bootburnLib, True)
            mcufw.ReadMcuFwParams()
            #bootburnLib.CopyFirmwareFiles(targetConfig.p_OutDirPath)
        targetConfig.p_TopOutDataPath = targetConfig.p_OutDirPath
        bootburnLib.CheckFuseForEncryption()

        if (targetConfig.n_FlashQnx):
            targetConfig.s_StorageDtbImgName = targetConfig.s_QnxDtbImgName
        else:
            targetConfig.s_StorageDtbImgName = targetConfig.s_LinuxDtbImgName

        targetConfig.sysMonitor.log("Generating Binaries in " + targetConfig.p_TopOutDataPath, True)

        if(targetConfig.parsed_commandline["toolsonly"]):
            toolsDir = os.path.join(targetConfig.p_OutDirPath, "tools", "flashtools", "flash")
            #targetConfig.BoardSetFilePathsAndDefaultValues()
            bootburnLib.HostSpaceCheck()
            bootburnLib.CopyTegraFlashUtilities(toolsDir)
            bootburnLib.CopyTegraFlashOfflineUtilities(targetConfig.p_OutDirPath)
        else:
            bootburnLib.SetOutPutDirName(targetConfig.p_TopOutDataPath)
            bootburnLib.CreateBSPImages()
            print("Calling CreatePlatInfo ")
            bootburnLib.CreatePlatInfoFile(targetConfig.p_OutDirPath, "TargetInfo.txt")
            bootburnLib.CopyTegraToolsVersionFile()

        targetConfig.sysMonitor.log("\033[01;32m \n\nImage creation has finished!\n\n \033[0m", True)
        end_time = time.time()
        total_time = end_time - start_time
        timeMessage = "Image Generation took {0:.2f} seconds\n".format(total_time)
        targetConfig.sysMonitor.log(timeMessage)
        #GVS will fail unless it gets this message
        print(timeMessage)

        return nverror.NvError_Success
    except Exception as e:
        bootburnLib.shellUtils.printError()
        print("\033[01;31mException {} raised in create_bsp_active \033[0m".format(str(e)))
        traceback.print_exc()
        if(isinstance(e.args[0], int)):
            return int(e.args[0])
        else:
            return nverror.NvError_Unknown

def create_bsp(commandLineArguments):
    try:
        debug = False

        GenericOptions = ['b', 'c', 'd', 'e', 'f', 'g', 'h', 'j', 'k', 'l', 'm', 'p', 'q', 'r',
                        's', 'u', 'B', 'C', 'D', 'E', 'L', 'M', 'R', 'T', 'X', 'Y',
                        'pytest', 'safety', 'toolsonly', 'user', 'vdk', 'fpga',
                        'encryption_key', 'encrypt', 'board_config', 'chain', 'asymmetric', 'customer_data', 'pl_encryption_key',
                        'hsm', 'init_persistent_partitions', 'disable-uart', "merge_chains",
                        "fskp_bct_path", "logs", "oot_kernel", "target_pvit", "l4t", "no_du_pvit"]

        DirBeingRunFrom = os.path.realpath(__file__)
        DirBeingRunFrom = os.path.dirname(DirBeingRunFrom)
        cwd = os.getcwd()
        os.chdir(DirBeingRunFrom)
        flashPath = os.path.join(DirBeingRunFrom, "..", "..","..","tools", "flashtools")
        sys.path.insert(0, DirBeingRunFrom)
        isPdkPackage = os.path.isdir(flashPath)
        targetConfig = target_config(isPdkPackage)
        targetConfig.p_BurnDir = os.getcwd()
        if(len(commandLineArguments) <= 1):
            targetConfig.sysMonitor = monitor("USAGE", "/tmp", debug)
            b = bootburn_lib.bootburn_lib(targetConfig)
            b.Usage(GenericOptions)
            print("\nERROR:  " + commandLineArguments[0] + " needs arguments, mandatory argument is board ID\n")
            AbnormalTermination("Missing arguments.  Must give at least the board ID in ", nverror.NvError_InvalidArgument)

        targetConfig.sysMonitor = monitor("create_bsp", targetConfig.p_logs, debug)
        cmdParser = commandline_parser(commandLineArguments)

        cmdParser.initializeTargetConfig(targetConfig, GenericOptions)
        boardName = ""
        if(cmdParser.parsed_commandline["h"] == False):
            boardName = "_" + targetConfig.s_BoardName
        targetConfig.sysMonitor = monitor("create_bsp" + boardName, targetConfig.p_logs, debug)

        from bootburn_thor import bootburn_thor
        bootburnLib = bootburn_thor(targetConfig)

        if (cmdParser.parsed_commandline["h"] == True):
            bootburnLib.Usage(GenericOptions)
            return nverror.NvError_Success

        cmdParser.validatePartialUpdateinCreateBsp(targetConfig)

        if (bootburnLib.targetConfig.mergeChains != None):
            # Print chains being merged
            print("Merging chains: ")
            for chainId, chainDir in list(bootburnLib.targetConfig.mergeChains.items()):
                print("\tChain_" + chainId + ": " + chainDir)

            bootburnLib.mergeChains()

            print("Merged output: " + bootburnLib.targetConfig.p_OutDirPath)
            return nverror.NvError_Success

        if (targetConfig.parsed_commandline["T"] == True):
            sys.settrace(bootburnLib.call_trace)
            targetConfig.sysMonitor.callTraceOut = True

        return create_bsp_active(bootburnLib, targetConfig)
    except Exception as e:
        print("\033[01;31m Exception {} raised in create_bsp_images \033[0m".format(str(e)))
        traceback.print_exc()
        if(isinstance(e.args[0], int)):
            return int(e.args[0])
        else:
            return nverror.NvError_Unknown
    finally:
        os.chdir(cwd)


def create_bsp_commandline():
    commandLineArguments = sys.argv
    print(str(commandLineArguments))
    return create_bsp(commandLineArguments)


if __name__ == "__main__":
    print("Please use '../bootburn/create_bsp_images.py' instead of this file.")
    exit()
    result = create_bsp_commandline()
    sys.exit(result)
