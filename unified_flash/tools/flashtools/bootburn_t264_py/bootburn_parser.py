#!/bin/usr/python
# SPDX-FileCopyrightText: Copyright (c) 2017-2026, NVIDIA CORPORATION.  All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary

#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

import os
import re
import sys
import json
import argparse
import traceback
from target_config import target_config, CustomerData
from bootburn_lib import bootburn_lib
from bootburn_bct import PlatformJsonDefines, LegacyDefines, StorageConfigParser
import subprocess
from flashtools_nverror import AbnormalTermination, nverror
from flash_utilities import shell_utilities
import shutil

class commandline_parser(object):

    # Format is "option": (#params, "Help Info", Default)
    input_options = {
        "-b": (1, "board name (mandatory)", True),  # BoardName
        "-d": (2, "Provide DTB file to be used. Can be done for multiple partitions.targetConfig.   -d <PARTITION_NAME> <filename>.targetConfig.   Possible values for PARTITION_NAME are bpmp-fw-dtb and kernel-dtb.targetConfig.   Default file is defined in BoardSetFilePathsAndDefaultValues.", False),
        "-g": (1, "Generate binaries at mentioned path only and do not flash.", False),
        "-h": (0, "help message", False),  # BoardName
        "-k": (1, "Absolute path of the native cfg file to be used.  If used with Hypervisor the cfg file must have been created from bind_partitions and be located in the hypervisor output direcory.   Default value is quickboot_qspi_linux.cfg for boot in case of linux.targetConfig.   Default value is quickboot_qspi_qnx.cfg for boot in case of qnx.targetConfig.   Default value is quickboot_qspi_integrity.cfg for boot in case of Integrity.targetConfig.", False),
        "-l": (0, "Use this option to flash native Linux cfg", False),
        "-m": (0, "Flashing Mods. This option enables mods specific-overrides.", False),
        "-o": (0, "skip flashing recovery partitions", False),  # n_SkipFlashingRecoveryParts
        "-p": (1, "Provide PKC Key file. Flash PKC secured image.", False),
        "-q": (0, "Use this option to flash native QNX cfg", False),
        "-r": (1, "Tegra Chip revision for which binaries are generated.targetConfig.   Supported revision are 01 and 02, Example: -r 01targetConfig.   -r is ignored when target is being flashed.", False),
        "-s": (0, "Skip the FS flashing.", False),
        "-t": (1, "Specify the output directory that will store the files being flashed.targetConfig.   Default name is _temp_dump with a random string appended at the end.", False),
        "-u": (-1, "Update only specified partition(s).targetConfig.   Pass the name of partition to be updated as described in the flashing cfg.targetConfig.   Example: -u kernel", False),
        "-x": (1, "Specify the aurix port for DDPX to put the tegras in recovery.", False),
        "-y": (0, "Don\'t pipeline Qspi0, Qspi1, Emmc, and UFS", False),
        "-B": (1, "Specify boot device. Supported boot device are qspi ,emmc and ufs. Default is qspi.", False),
        "-C": (0, "Use debug binaries for Bootloader", False),
        "-D": (0, "Enable debug prints of the Flashing script.", False),
        "-E": (0, "Use this option to Enable DRAM ECC.", False),
        "-I": (-1, "Flash specific target.targetConfig.   When multiple targets are connected to your host use the Bus & Device number oftargetConfig.   the device to differentiate among various devices to specify target to flash.targetConfig.   Example: -I 003 043 ...", "?"),
        "-M": (0, "internal development board", False),  # n_EngSample
        "-O": (0, "Fast Flash recovery partitions", False),  # n_FastFlash
        "-P": (1, "prebuild binaries directory", False),  # OutDirPath
        "-R": (0, "RCM mode", False),  # n_RcmMode
        "-T": (0, "Enable tracing of bootburn operations to a trace log file.", False),
        "-U": (1, "Pass in the UFS provision cfg file.", False),
        "-V": (0, "readback verification", False),  # .n_RBVerification
        "-X": (0, "Enable Golden Register Address:Value Dump into GR memory carveout.", False),
        "-Y": (1, "Specific dt-overlay odm-data parameter.", False),
        "--adbSerial": (1, "specify ADB seiral number of the target device", False),
        "--applet":(1, "specify command to be sent to the applet", False),
        "--asymmetric": (0, "Used to generate images for native chain in asymmetric mode", False),
        "--board_config": (1, "User defined bootburn helper json file.", False),
        "--chain": (1, "Only Generate Chain for Either A or B", "?"),
        "--clean": (0, "Clean/Full Erase of Non-volatile Storage Device", False),
        "--customer-data": (-1, "Customer data value and optional customer data value json file: <Customer data json file> [<Customer data schema file>]", False),
        "--dev": (0, "This is a development branch where targetConfig.TOP is set", False),
        "--devicetype": (1, "Only flash devices of type spi, sdhci or ufshci", "?"),
        "--disable_uart": (0, "disable UART if the value is true", False),
        "--encrypt": (0, "Global switch to enable encryption", False),
        "--filesystem":(1, "For PDK installer to pass location of root FS file system.  The --platform option must be used in conjunction with this call", False),
        "--find_board_name": (1, "find connected board name", False),
        "--fpga":(0, "Create flash images for the FPGA bring up", False),
        "--fskp-bct-path": (-1,
                       "Path to fskp bct (Required only for asymmetric boot chains)>",
                       False
                      ),
        "--headers": (1, "Headers directory path for new BCH headers and BCT", "?"),
        "--hsm": (1, "Use HSM with either [rsa|eddsa]+sbk+kek0 ex rsa+sbk+kek0", False),
        "--init_persistent_partitions": (0, "Overwrite Persistent Partitions", False),
        "--l4t":(0, "Create flash images for l4t", False),
        "--logs": (1, "Location of where to place the log files", "?"),
        "--merge-chains": (-1,
                           "Merge chains generated using --chain arguments. Arguments: Space separtred list of the format <chain_id> = Absolute path to chain output directory",
                           False
                          ),
        "--multi-die":(0, "Flashing for multisocket images", False),
        "--no_du_pvit":(0, "Disable generation of DU only entries in PVIT", False),
        "--no_firmware_update":(0, "Force block all firmware updates."),
        "--oot_kernel": (0, "Flash OOT dtb to target", False),
        "--platform":(1, "For PDK installer to pass location of prebuilt images created with create_bsp_images.py.", False),
        "--safety": (0, "This is production safety flashing.  Certain options are not allowed to comply to TCL-1 classification", False),
        "--skipBctPreserve": (0, "Skip Br Bct Read", False),
        "--target_pvit": (-1, "PVIT partition on the target, user to provide as reference for partial update", "?"),
        "--toolsonly":(0, "Output tools to output directory only.  must use the -g option to specify the location to place the tools."),
        "--usb-instance":(-1, "The USB port path to flash", None),
        "--user": (1, "User who called the application to chown files to", False),
        "--vdk":(0, "Create flash images for the VDK simulator", False),
        "--l4t_boot_chain_select":(1, "Specify the boot chain (ex. \"A\" or \"B\" ) for L4T", "A"),
        }

    safetyAllowedOptions = ['b', 'g', 'h', 'l', 'p', 'r', 't', 'u', 'x', 'z', 'A', 'B', 'D', 'E',
                            'I', 'P', 'R', 'asymmetric', 'board_config', 'chain', 'clean', 'customer_data', 'devicetype',
                            'encrypt', 'encryption_key', 'find_board_name', 'fskp_bct_path', 'hsm',
                            'init_persistent_partitions', 'logs', 'merge_chains', 'no_du_pvit',
                            'no_firmware_update', 'pl_encryption_key', 'safety', 'target_pvit', 'user', "l4t_boot_chain_select"]

    boardJsonData = {}
    user_commandline = ""
    parsed_commandline = []
    parser = None
    shellUtils = shell_utilities()
    TEGRA_TOP = ""

    def __init__(self, userInput):
        self.user_commandline = userInput
        self.parser = argparse.ArgumentParser(description="Parser", add_help=False)
        # add options to the python parser
        # for option, (numArguments, helpMessage, mandatory) in self.input_options:
        for option, arg in self.input_options.items():
            numArguments = arg[0]
            helpMessage = arg[1]
            if numArguments > 0:
                self.parser.add_argument(option, help=helpMessage, nargs=numArguments)
            elif numArguments == False:
                self.parser.add_argument(option, help=helpMessage, action="store_true")
            else:
                self.parser.add_argument(option, help=helpMessage, nargs="+")

        # have python actually parse the commandline input
        self.parsed_commandline = self.parser.parse_args(self.user_commandline[1:])
        self.parsed_commandline = vars(self.parsed_commandline)
        # only two  mandatory options are board name, find_board_name apart from help
        if ((self.parsed_commandline["h"] == False) and
            (not self.parsed_commandline["b"]) and  self.parsed_commandline["find_board_name"] == None):
            errorStr = "Must specify the help or board type or find board name with option (-h) or (-b) or (--find_board_name)\nMandatory option -b  or -h or --find_board_name is  not given"
            AbnormalTermination(errorStr , nverror.NvError_InvalidArgument)

    def overrideBoardName(self, name):
        if(isinstance(self.parsed_commandline['b'], list)):
           self.parsed_commandline['b'][0] = name
        else:
            self.parsed_commandline['b'] = [name]

        print(self.parsed_commandline['b'][0])

    def checkParallelCvm(self, targetConfig):
        boardName = self.parsed_commandline['b'][0].split('-')

        if(len(boardName) == 3 and "e3550" in boardName[0]):
            validBoard = boardName[0] in ['e3550b01', 'e3550b03']
            validChip = boardName[1] in ['t194', 't194a', 't194b']
            isCvm = boardName[2] == 'cvm'

            if (not validBoard) or (not validChip) or (not isCvm):
                AbnormalTermination("Invalid board name " + self.parsed_commandline['b'][0],
                                    nverror.NvError_InvalidArgument)

            if not targetConfig.TEST_AUTOMATION:
                AbnormalTermination("Parallel flashing of " + self.parsed_commandline['b'][0] + " is supported only in GVS",
                                    nverror.NvError_InvalidArgument)

            targetConfig.s_Tegra_C = True
            targetConfig.s_BoardName = boardName[0] + '-' + boardName[1]

    def initializeTargetConfig(self, targetConfig, genericOptions=None, parsePlatform=True):
        self.checkSafety(targetConfig)
        if(self.parsed_commandline["h"] == True):
            print(self.parsed_commandline["logs"])
            if (self.parsed_commandline["logs"] != None):
                print(self.parsed_commandline["logs"])
                self.SetLogDir(targetConfig, self.parsed_commandline["logs"])
            return
        if len(targetConfig.s_BoardName) == 0 and (not self.parsed_commandline["find_board_name"]):
            targetConfig.s_BoardName = self.parsed_commandline['b'][0]
        try:
            if(self.parsed_commandline["find_board_name"] and (len(targetConfig.s_baseBoardName) == 0)):
                self.setTargetConfigData(targetConfig, genericOptions)
                targetConfig.SetDefaultPaths()
                if(not targetConfig.TEST_AUTOMATION):
                    self.setBoardConfigPath(targetConfig)
            elif("--toolsonly" in sys.argv):
                self.setTargetConfigData(targetConfig, genericOptions)
                targetConfig.SetDefaultPaths()
            else:
                self.setTargetConfigData(targetConfig, genericOptions)
                targetConfig.SetDefaultPaths()
                self.setBoardConfigPath(targetConfig)
                self.loadBoardGoldenRegsFile(targetConfig)
                if(parsePlatform):
                    self.parsePlatformConfig(targetConfig)
                self.ValidateParameters(targetConfig)
                print("Done parsing command line\n")

            #After board config read in get RAMCODE
            targetConfig.JsonInitRamcode()
            if(targetConfig.TEST_AUTOMATION == True):
                targetConfig.parsed_commandline["no_firmware_update"] = True

        except Exception as e:
            print(str(e))
            AbnormalTermination("Failed to initialize target config", nverror.NvError_ConfigVarNotFound)

    def loadBoardGoldenRegsFile(self, targetConfig):
        # Golden Registers Info
        print("cwd in loadBoardGoldenRegsFile :: " +  os.getcwd())
        goldenregs = os.path.join(targetConfig.p_BoardConfigsPath, "goldenregs.json")
        fid = open(goldenregs, "r")
        data = fid.read()
        fid.close()
        boardJsonData = json.loads(data)
        targetConfig.f_BoardConfigFile = self.loadBoardConfigFile(targetConfig)
        fid = open(targetConfig.f_BoardConfigFile, "r")
        data = fid.read()
        fid.close()
        boardJsonData.update(json.loads(data))
        self.boardDefaultPaths = boardJsonData[targetConfig.s_BoardName]
        targetConfig.boardDefaultPaths = boardJsonData[targetConfig.s_BoardName]
        if("s_MediaCombination" in self.boardDefaultPaths) and ("MediaCombinations" in boardJsonData):
            media = self.boardDefaultPaths["s_MediaCombination"]
            targetConfig.mediaCombination = boardJsonData["MediaCombinations"][media]

    def setBoardConfigPath(self, targetConfig):
        print("cwd in setBoardConfigPath :: " + os.getcwd())
        if(self.parsed_commandline["board_config"] != None):
            userHelperFile = self.parsed_commandline["board_config"][0]
        if len(targetConfig.s_BoardName) == 0 and (not self.parsed_commandline["find_board_name"]):
            targetConfig.s_BoardName = self.parsed_commandline['b'][0]

        if (targetConfig.isPDK == True) and (targetConfig.BOOTBURN_PYTEST == False):
            board_configs = "../board_configs"
        else:
            board_configs = "../../board_configs"
        targetConfig.p_BoardConfigsPath = os.path.join(os.getcwd(), board_configs)

        board_configs = ""
        isminiPDK = os.path.isfile( os.path.join(os.getcwd(), "..", "board_configs","baseboard_sku_mapping.json"))
        if (sys.argv[0].find('flash_bsp_images') !=  -1) and (isminiPDK or (targetConfig.TEST_AUTOMATION)):
            print ("*** miniPDK in setBoardConfigPath  ****")
            board_configs = "../board_configs"
            board_configs =  os.path.join(os.getcwd(), board_configs)
            print("offline binary flashing (flash_bsp) board_config path is derived from cwd :: " + board_configs)
            targetConfig.p_SocBoardConfigsPath = board_configs

    def loadBoardConfigFile(self, targetConfig):
        userHelperFile = ""
        boardConfigFile = targetConfig.s_BoardName + ".json"
        if(self.parsed_commandline["board_config"] != None):
            userHelperFile = self.parsed_commandline["board_config"][0]
        elif(targetConfig.is_l4t or targetConfig.is_sim):
            userHelperFile = os.path.join(targetConfig.p_SocBoardConfigsPath, boardConfigFile)
        else:
            searchPath = targetConfig.p_SocBoardConfigsPath
            userHelperFile = self.shellUtils.find(searchPath, boardConfigFile, True)
            userHelperFile = userHelperFile[0]

        if(os.path.isfile(userHelperFile) == False):
            AbnormalTermination("Can't find board config file " + userHelperFile)
        return userHelperFile

    def setTargetConfigData(self, targetConfig, genericOptions=None):
        targetConfig.parsed_commandline = self.parsed_commandline
        # set the target configuration values from the input
        targetConfig.f_customerDataSchemaFile = os.path.join(os.getcwd(), "nv-customer-data-schema.json")
        print("Default Schema:%s\n" %(targetConfig.f_customerDataSchemaFile))
        for option in self.parsed_commandline:
            argument = self.parsed_commandline[option]
            if(genericOptions and argument and ((option in genericOptions) == False)):
                errorMsg = "Option " + option + " is not a valid choice with " + os.path.basename(sys.argv[0])
                print(errorMsg)
                AbnormalTermination(errorMsg, nverror.NvError_InvalidArgument)

        self.setTargetConfigDataCmdLine(targetConfig, self.parsed_commandline)

    def setTargetConfigDataCmdLine(self, targetConfig, commandLine):

        # set the target configuration values from the input
        for option in commandLine:

            argument = commandLine[option]
            if(option[0] == '-'):
                #platform config includes hyphens, need to remove them
                option = option[1:]

            if(option == "k" and not argument == None):
                targetConfig.f_CustomFlashCfg = argument[0]
                if(os.path.isfile(targetConfig.f_CustomFlashCfg) == False):
                    print("CUSTOM FLASH CFG: " + targetConfig.f_CustomFlashCfg + " does not exist")
            elif (option == "asymmetric" and argument == True):
                targetConfig.n_Asymmetric = True
            elif(option == "l" and argument == True) or (targetConfig.b_findBoardinNativeLinux == True):
                targetConfig.n_FlashLinux = True
                targetConfig.n_FlashHypervisor = False
                targetConfig.s_DtbImgSuffix = "-linux-native"

            elif(option == "D" and argument == True):
                targetConfig.s_debugOutput = argument
                self.f_OutFile = "stdout"

            elif(option == "B" and not argument == None):
                bootDevice = argument[0]
                if(bootDevice == "emmc"):
                    bootDevice = "sdmmc"
                targetConfig.s_BootDevice = bootDevice

            elif(option == "V" and argument == True):
                targetConfig.n_RBVerification = argument

            elif(option == "U" and not argument == None):
                if(not argument or not os.path.isfile(argument[0])):
                    print("invalid UFS config file with option (-U)\n")
                    raise Exception("Invalid UFS Config")

                targetConfig.f_UfsProvisionCfg = argument[0]
                targetConfig.n_UFSProvisioningEnable = True

            elif(option == "q" and argument == True) or (targetConfig.b_findBoardinNativeQnx == True):
                targetConfig.n_FlashQnx = True
                targetConfig.n_FlashHypervisor = False
                targetConfig.s_DtbImgSuffix = "-qnxwrap"

            elif(option == "u" and not argument == None):
                targetConfig.s_UpdatePartitions = argument

            elif(option == "safety"):
                targetConfig.SAFETY_BUILD = argument

            elif(option == "M"):
                targetConfig.n_EngSample = argument

            elif(option == "C"):
                targetConfig.n_UseDebugBins = argument

            elif(option == "g" and not argument == None):
                outputPath = os.path.abspath(argument[0])
                targetConfig.p_OutDirPath = outputPath
                targetConfig.p_TempDumpPath = outputPath
                targetConfig.n_GenBinsOnly = True
                targetConfig.n_SkipSkuValidate = True

            elif(option == "P" and not argument == None):
                path = argument[0]
                if(not os.path.isdir(path)):
                    print("OUT DIR PATH : the path - " + path + " - is not valid path\n")
                    AbnormalTermination("Invalid image path given with -P option", nverror.NvError_FileNotFound)

                path=os.path.abspath(path)
                targetConfig.p_TempOutDir = path
                targetConfig.p_OutDirPath = path

            elif(option == "r" and not argument == None):
                chipVersion = int(argument[0])
                if(chipVersion < 1 or chipVersion > 2):
                    print("Invalid Chip version " + str(chipVersion))
                    AbnormalTermination("Invalid chip version must be either 1 or 2", nverror.NvError_InvalidArgument)
                targetConfig.s_ChipRevision = argument[0]
                targetConfig.n_ChipVersion = chipVersion
                targetConfig.n_ChipVersionSet = True

            if(option == "d" and not argument == None):
                partition = argument[0]
                dtb = argument[1]

                if(partition == "bpmp-fw-dtb"):
                    targetConfig.f_BpmpDtbName = dtb

                elif(partition == "kernel-dtb"):
                    targetConfig.f_StorageDtbName = dtb
                else:
                    AbnormalTermination("s_ERROR_OPTION_VALUE_INVALID -- option d invalid name", nverror.NvError_BadParameter)

            elif(option == "p" and not argument == None):
                if(not os.path.isfile(argument[0])):
                    print("the key file path for option (-p) - " + argument[0] + " - is not valid path\n")
                    raise Exception("Key file does not exist")
                keyFile = os.path.abspath(argument[0])
                targetConfig.f_PkcKeyFilePath = keyFile
                targetConfig.n_SecurebootFlash = True

            elif(option == "encryption_key" and not argument == None):
                if(not os.path.isfile(argument[0])):
                    print("the key file path for option (-N) - " + argument[0] + " - is not valid path\n")
                    raise Exception("Key file does not exist")
                keyFile = os.path.abspath(argument[0])
                targetConfig.f_EncKeyFilePath = keyFile

            elif(option == "encrypt" and argument == True):
                targetConfig.f_EncKeyFilePath = ""

            elif(option == "pl_encryption_key" and not argument == None):
                if(not os.path.isfile(argument[0])):
                    print("the key file path for option (-N) - " + argument[0] + " - is not valid path\n")
                    raise Exception("Key file does not exist")
                keyFile = os.path.abspath(argument[0])
                targetConfig.f_PlEncKeyFilePath = keyFile

            elif(option == "hsm" and not argument == None):
                choices = ['rsa', 'ecc521', 'ecc', 'eddsa', 'xmss', 'sbk']
                found = False
                for choice in choices:
                    if choice in argument[0]:
                        found = True
                if not found:
                    print(f"warning hsm only supports {choices}  {argument[0]} - is not valid cfg\n")
                    raise Exception("HSM configuration not supported")

                if ('sbk' in argument[0]):
                    targetConfig.f_EncKeyFilePath = os.path.join(os.getcwd(), 'encrypt_sbk_dummy_key.txt')
                    if ('kek0' in argument[0]):
                        targetConfig.f_PlEncKeyFilePath = os.path.join(os.getcwd(), 'encrypt_kek0_dummy_key.txt')
                elif ('kek0' in argument[0]):
                    raise Exception("HSM configuration kek0 not supported without sbk")

                choices.pop()
                targetConfig.HSMAuthStr = 'zero'

                for choice in choices:
                    if choice not in argument[0]:
                        continue
                    targetConfig.f_PkcKeyFilePath = os.path.join(os.getcwd(), f"sign_{choice}_dummy_key.txt")
                    targetConfig.n_SecurebootFlash = True
                    m = re.search(rf'[\d]+{choice}', argument[0])
                    if m:
                        targetConfig.HSMAuthStr = m.group(0)
                    else:
                        targetConfig.HSMAuthStr = choice
                    targetConfig.HSMStr=(" --hsm")
                    break

            elif(option == "E" and argument == True):
                targetConfig.n_DRAMECCEnabled = argument

            elif(option == "t" and not argument == None):
                targetConfig.p_OutDirPath = os.path.abspath(argument[0])

            elif(option == "Y" and not argument == None):
                targetConfig.s_DtOdmdata = argument[0]
            elif(option == "I" and not argument == None):
                if (self.parsed_commandline["multi_die"]):
                    busNumbers = [argument[0], argument[2]]
                    deviceNumbers = [argument[1], argument[3]]
                else:
                    busNumbers = [argument[0]]
                    deviceNumbers = [argument[1]]

                if (not targetConfig.parsed_commandline["multi_die"] and targetConfig.parsed_commandline["x"]):
                    errorStr = "-I and -x are mutually exclusive parameters"
                    AbnormalTermination(errorStr, nverror.NvError_BadParameter)

                for index, (busNumber, deviceNumber) in enumerate(zip(busNumbers, deviceNumbers)):
                    name = "die" + str(index)

                    path = "/dev/bus/usb/" + busNumber + "/" + deviceNumber
                    shellCommand = "udevadm info -q path -n " + path
                    portPath = self.shellUtils.executeShellCommand(shellCommand, True, True)
                    if (isinstance(portPath, int)):
                        errMsg = "failed to get the port path for " + path
                        AbnormalTermination(errMsg, nverror.NvError_SystemCommandFailed)
                    portPath = "/sys" + portPath

                    usbData = {
                            "bus":busNumber,
                            "device":deviceNumber,
                            "path":path,
                            "adbSerial":"",
                            "name":name,
                            "portPath":portPath,
                            "boardName":self.parsed_commandline['b'][0]
                    }
                    targetConfig.s_TargetDeviceInfo[name] = usbData

            elif(option == "x" and not argument == None):
                if (targetConfig.parsed_commandline["I"]):
                    errorStr = "-I and -x are mutually exclusive parameters"
                    AbnormalTermination(errorStr, nverror.NvError_BadParameter)

                targetConfig.s_McuPort = argument[0]
                if (os.path.exists(targetConfig.s_McuPort) == False):
                    print("Aurix port ${s_McuPort} does not exist")
                    AbnormalTermination("${s_ERROR_DEVICE_NOT_FOUND} --  " + targetConfig.s_McuPort, nverror.NvError_DeviceNotFound)

                targetConfig.s_AurixLockFile = "/var/lock/LCK..`basename $s_McuPort`"

            elif(option == "adbSerial" and argument != None):
                targetConfig.s_AdbSerialNum = argument[0]

            elif(option == "applet" and argument != None):
                targetConfig.s_AppletCmd = argument[0]

            elif(option == "pytest" and argument == True):
                targetConfig.BOOTBURN_PYTEST = True

            elif(option == "X" and argument == True):
                targetConfig.n_GrDump = True

            elif(option == "no_du_pvit" and argument == True):
                targetConfig.n_NoDuPvit = True

            elif(option == "chain" and argument != None):
                chain = argument[0]
                if ((chain != "A") and (chain != "B") and (chain != "C")):
                    errorStr = str(chain) + "is not a valid chain. Provide a valid chain, A or B or C only"
                    AbnormalTermination(errorStr, nverror.NvError_BadParameter)

                if (self.parsed_commandline['asymmetric'] and chain == 'A'):
                    # Check if --fskp-bct-path is specified
                    if (self.parsed_commandline["fskp_bct_path"] == None):
                        raise AbnormalTermination(
                            "FSKP BCT path must be specified for asymmetric chain A",
                            nverror.NvError_InvalidArgument
                        )

                targetConfig.generateChain = chain
            elif (option == "customer_data" and argument != None):
                self.handleCustomerDataArgs(argument, targetConfig)

            elif (option == "merge_chains" and argument != None):
                if (len(argument) < 2):
                    errStr = "There has to be atleast two chains for merging"
                    AbnormalTermination(errStr, nverror.NvError_BadParameter)

                # Store the directories per chain
                targetConfig.mergeChains = { arg.split('=')[0].strip(): arg.split('=')[1].strip() for arg in argument[:] }

                # Check for directories
                for inputDir in list(targetConfig.mergeChains.values()):
                    if (not os.path.exists(inputDir)):
                        AbnormalTermination(inputDir + " directory not found!", nverror.NvError_BadParameter)

            elif (option == "fskp_bct_path" and argument != None):
                if (not targetConfig.n_Asymmetric):
                    raise AbnormalTermination("--fskp-bct-path can only be specified in asymmetric mode")

                # Check if BR BCT exists
                if (not os.path.exists(argument[0])):
                    raise AbnormalTermination("FSKP BCT is not found!", nverror.NvError_InvalidArgument)

                targetConfig.fskpBctPath = argument[0]

            elif (option == "init_persistent_partitions" and not argument == None):
                targetConfig.b_CreateOptionalPartitions = argument
            elif (option == "disable_uart" and  argument == True):
                targetConfig.b_DisableUARTinMB1nMB2Bct = True
                print (" *****  found disable_uart in bootburn-flags : " + str(targetConfig.b_DisableUARTinMB1nMB2Bct) + "  ****** ")
            elif(option == "devicetype" and argument != None):
                devicetype = argument[0]
                if ((devicetype != "spi") and (devicetype != "sdhci") and (devicetype != "ufshci")):
                    errorStr = "devicetype can only be spi, sdhci or ufshci " + devicetype
                    AbnormalTermination(errorStr, nverror.NvError_BadParameter)

                targetConfig.devicetype = devicetype
            elif(option == "logs" and argument != None):
                self.SetLogDir(targetConfig, argument)
            elif(option == "find_board_name") and (argument != None):
                if (argument[0] == "thor"):
                    targetConfig.b_findBoardName = True
                else:
                    errorStr = "Invalid board type provided with --find_board_name thor|orin"
                    AbnormalTermination(errorStr, nverror.NvError_BadParameter)
                if (not targetConfig.parsed_commandline["x"]):
                    errorStr = "-x option need to be provided with --find_board_name option"
                    AbnormalTermination(errorStr, nverror.NvError_BadParameter)
                if (not targetConfig.parsed_commandline["P"]  and "flash_bsp" in sys.argv[0]):
                    errorStr = "-P option need to be provided with --find_board_name option in flash_bsp_images.py"
                    AbnormalTermination(errorStr, nverror.NvError_BadParameter)
            elif(option == "headers" and  argument != None):
                targetConfig.headers = argument[0]
            elif(option == "target_pvit" and not argument == None):
                targetConfig.f_refPvitFile = argument
                for pvitFile in targetConfig.f_refPvitFile:
                    if not os.path.isfile(pvitFile):
                        print("the target pvit file path -  " + pvitFile + " in target_pvit command option is not a valid path\n")
                        raise Exception("referece_pvit file does not exist")
            elif(option == "skipBctPreserve" and argument == True):
                targetConfig.skipBctPreserve = True
            elif(option == "l4t" and argument==True):
                targetConfig.is_l4t = True
                targetConfig.n_FlashHypervisor = False
            elif(option == "usb_instance" and argument):
                if (self.parsed_commandline["multi_die"]):
                    portPaths = [argument[0], argument[1]]
                else:
                    portPaths = [argument[0]]

                if (not targetConfig.parsed_commandline["multi_die"] and targetConfig.parsed_commandline["x"]):
                    errorStr = "--usb-instance and -x are mutually exclusive parameters"
                    AbnormalTermination(errorStr, nverror.NvError_BadParameter)

                for index, (portPath) in enumerate(portPaths):
                    name = "die" + str(index)

                    path = "/sys/bus/usb/devices/" + portPath
                    busNumber = None
                    devNumber = None
                    with open(path + "/busnum", "r") as busNumberFile, open(path + "/devnum", "r") as devNumberFile:
                        busNumber = busNumberFile.read()
                        devNumber = devNumberFile.read()

                    if not busNumber or not devNumber:
                        errorStr = "Cannot retrieve bus number and dev number"
                        AbnormalTermination(errorStr, nverror.NvError_InvalidState)
                    devPath = "/dev/bus/usb/{:03d}/{:03d}".format(int(busNumber), int(devNumber))
                    shellCommand = "udevadm info -q path -n " + devPath
                    portPathUdev = self.shellUtils.executeShellCommand(shellCommand, True, True)
                    if (isinstance(portPathUdev, int)):
                        errMsg = "failed to get the port path for " + path
                        AbnormalTermination(errMsg, nverror.NvError_SystemCommandFailed)
                    portPathUdev = "/sys" + portPathUdev

                    usbData = {
                            "bus":busNumber,
                            "device":devNumber,
                            "path": devPath,
                            "adbSerial":"",
                            "name":name,
                            "portPath":portPathUdev,
                            "boardName":self.parsed_commandline['b'][0]
                    }
                    targetConfig.s_TargetDeviceInfo[name] = usbData
            elif(option == "vdk" and argument==True):
                targetConfig.is_sim = True
            elif(option == "l4t_boot_chain_select" and argument):
                targetConfig.s_L4TBootChainSelect = argument[0].upper()

        #Check commandline parameters combinations
        if targetConfig.generateChain and targetConfig.s_UpdatePartitions:
            imageChain = targetConfig.generateChain + "_"
            for imageFile in targetConfig.s_UpdatePartitions:
                # Partition in Chain we want to generate
                if imageChain in imageFile:
                    continue
                # Is this in L1 global? Any chain will work
                if "A_" not in imageFile and "B_" not in imageFile and "C_" not in imageFile:
                    continue

                # Not in chain we are generating
                errorStr = "Chain " + str(targetConfig.generateChain) + " is not compatible with " + imageFile
                AbnormalTermination(errorStr, nverror.NvError_InvalidArgument)

        if targetConfig.s_UpdatePartitions:
            ptParts = ""
            for part in targetConfig.s_UpdatePartitions:
                if (part == "pt") or ("_pt" in part):
                    ptParts = ptParts + part + ", "
            if (len(ptParts) != 0):
                errorStr = "PT imagess are not allowed in partial flashing. Remove " +  ptParts + " from -u option list"
                AbnormalTermination(errorStr, nverror.NvError_InvalidArgument)
            if targetConfig.parsed_commandline["d"] and  targetConfig.s_UpdatePartitions:
                if targetConfig.dtbPartition not in  targetConfig.s_UpdatePartitions:
                    errorStr = "dtb  in -d option is not part of partition list of -u option"
                    AbnormalTermination(errorStr, nverror.NvError_InvalidArgument)
            if targetConfig.parsed_commandline["s"]:
                errorStr = "-s option to skipFileSystem is not allowed in partial update (-u option)"
                AbnormalTermination(errorStr, nverror.NvError_InvalidArgument)

        #find out userpvit for all the selected chains is available or not
        #abort if not provided with -p and -u options
        #Getchain information from command line parameters, if provided

        if (targetConfig.generateChain != None) and (len(targetConfig.generateChain.strip()) != 0):
            targetConfig.l_selectedChains.append(targetConfig.generateChain.strip())
        else:
            for partition in targetConfig.s_UpdatePartitions:
                if (partition[1] == '_') and partition[0] not in targetConfig.l_selectedChains:
                    targetConfig.l_selectedChains.append(partition[0])
    def validatePartialUpdateinCreateBsp(self, targetConfig):
        if (targetConfig.s_UpdatePartitions  and "create_bsp" in sys.argv[0]):
            for chain in targetConfig.l_selectedChains:
                pvitEnabled = True
                if not(targetConfig.platformConfig != None and targetConfig.platformConfig.get('bct_defines') != None and "ENABLE_PVIT" in targetConfig.platformConfig['bct_defines']["chains"]["chain_"+chain]):
                    pvitEnabled = False
                usrPvitnotFound = True
                for usrPvit in targetConfig.f_refPvitFile:
                    if (chain in usrPvit):
                        if not pvitEnabled:
                            errorStr = "userPvit provided with --target-ref for Chain with ENABLE_PVIT=n for chain " + str(chain)
                            AbnormalTermination(errorStr, nverror.NvError_InvalidArgument)
                        usrPvitnotFound = False
                        break
                if usrPvitnotFound and pvitEnabled:
                    if  self.findAllPartTgtPvitNeeded(targetConfig, chain):
                        errorStr = "No userPvit provided with --target-ref for Chain " + str(chain)
                        AbnormalTermination(errorStr, nverror.NvError_InvalidArgument)

    def findAllPartTgtPvitNeeded(self, targetConfig, chain):
        pvitNeeded = True
        pvitValidateinPartial = 0
        attrName = "pvit_validate"
        value = ""
        newParentCfg = os.path.join(os.getcwd(), "tmp_" + os.path.basename(targetConfig.f_FlashCfg))

        self.storageCfgParser = StorageConfigParser(targetConfig)
        self.storageCfgParser.parseConfigFile(targetConfig.f_FlashCfg, newParentCfg)
        for partName in targetConfig.s_UpdatePartitions:
            if (partName[1] == '_') and partName[0] == chain:
                retval, value =  self.storageCfgParser.findPartAttribVal(partName, attrName)
                if retval:
                    if (value == "no"):
                        pvitValidateinPartial += 1
                    else:
                        targetConfig.sysMonitor.vlog("partName " + partName + " with pvit_validate value " + value)
                else:
                    targetConfig.sysMonitor.vlog("partName " + partName + " doesn't have  pvit_validate  attribute")

        if (pvitValidateinPartial == len(targetConfig.s_UpdatePartitions) ):
            pvitNeeded = False

        #if all partitions return "no" for pvit_validate, then retval is False
        return  pvitNeeded

    def handleCustomerDataArgs(self, argumentList, targetConfig):
        if (not os.path.exists(argumentList[0])):
            errStr = "Customer data value file {} not found!".format(
                argumentList[0])
            AbnormalTermination(errStr, nverror.NvError_BadParameter)

        targetConfig.f_customerDataValueFile = os.path.abspath(argumentList[0])

        if (len(argumentList) == 2):
            if (not os.path.exists(argumentList[1])):
                errStr = "Customer data schema file %s not found!".format(
                    argumentList[1])
                AbnormalTermination(
                    errStr, nverror.NvError_BadParameter)

            targetConfig.f_customerDataSchemaFile = os.path.abspath(argumentList[1])
            try:
                targetConfig.customerDataProcessor.load_schema(
                    targetConfig.f_customerDataSchemaFile)
            except Exception as err:
                errStr = "Error loading customer schema value file: {}!".format(
                    argumentList[1])
                AbnormalTermination(
                    errStr, nverror.NvError_BadParameter)
        else:
            # Load the default schema
            targetConfig.customerDataProcessor.load_schema(targetConfig.f_customerDataSchemaFile)

        # Load unsigned and/or signed customer data
        with open(argumentList[0], "r") as f_custData:
            customerData = json.load(f_custData)
            targetConfig.signedCustomerData = CustomerData(
                                                customerData.get('customer-data-signed', {}),
                                                "customer-data-signed"
                                            )
            targetConfig.customerData = CustomerData(
                                            customerData.get('customer-data-unsigned', {}),
                                            "customer-data-unsigned"
                                        )

        # Also copy mac ids and serial number from signed to unsigned data
        # if macid0-7 and serial id are present in signed data, copy them to unsigned
        signedToUnsignedList = ["macInUseCount", "macId0", "macId1", "macId2", "macId3",
                                "macId4", "macId5", "macId6", "macId7",
                                "boardSerial"]
        for key in signedToUnsignedList:
            # Check to make sure unsigned data doesn't have mac ids and board serial
            if (targetConfig.customerData.hasKey(key)):
                raise AbnormalTermination(
                    "Any of %s should not be present in unsigned data" % signedToUnsignedList,
                    nverror.NvError_BadParameter
                )
            if (targetConfig.signedCustomerData.hasKey(key)):
                targetConfig.customerData.setKeyValue(key, targetConfig.signedCustomerData.getValue(key))

    def SetLogDir(self, targetConfig, argument):
        outputPath = os.path.abspath(argument[0])
        try:
            if not os.path.exists(outputPath):
                os.makedirs(outputPath)
        except Exception as e:
            AbnormalTermination("failed to create dir " + outputPath, nverror.NvError_FileOperationFailed)
        targetConfig.p_logs = outputPath

    def ValidateParameters(self, targetConfig):
        n_SkipFlashingRecoveryParts = targetConfig.parsed_commandline["o"]
        n_FastFlash = targetConfig.parsed_commandline["O"]
        if (n_FastFlash):
            n_SkipFlashingRecoveryParts = n_FastFlash
        n_RcmMode = targetConfig.parsed_commandline["R"]
        n_AurixPort = targetConfig.parsed_commandline["x"]
        n_customerData = targetConfig.parsed_commandline["customer_data"]
        p_platform = None
        p_filesystem = None

        if(targetConfig.parsed_commandline["filesystem"]):
            p_filesystem = targetConfig.parsed_commandline["filesystem"][0]

        if(p_filesystem != None and p_platform == None):
            errorStr = "Musts specify the platform when specifying the file system"
            print(errorStr)
            AbnormalTermination(errorStr, nverror.NvError_BadParameter)
        elif(p_platform != None and p_filesystem == None):
            errorStr = "Musts specify the file system when specifying the platform"
            print(errorStr)
            AbnormalTermination(errorStr, nverror.NvError_BadParameter)
        elif(p_platform != None):
            if(os.path.isdir(p_platform) == False):
                errorStr = "Platform directory does not exist - " + p_platform
                print(errorStr)
                AbnormalTermination(errorStr, nverror.NvError_FileNotFound)
            elif(os.path.isdir(p_filesystem) == False):
                errorStr = "File system directory does not exist - " + p_filesystem
                print(errorStr)
                AbnormalTermination(errorStr, nverror.NvError_FileNotFound)

            #done validating platform
            return

        boardName =  targetConfig.s_BoardName.strip()
        SOCtoFlash = boardName.split('-')[1].strip()[-1]

        if(boardName.find("e3550") != -1):
            if (SOCtoFlash != "a" and SOCtoFlash != "b" and SOCtoFlash != "c") or ("cvm" in boardName):
                if(n_AurixPort == None):
                    errorStr = "If parallel flashing is desired you need to also include the Aurix port with the -x option"
                    print(errorStr)
                    AbnormalTermination(errorStr, nverror.NvError_BadParameter)
        print("Check finished successfully")
        if(not os.path.isfile(targetConfig.f_OutFile)):
            if(targetConfig.f_OutFile != "/dev/stdout"):
                self.shellUtils.removeFile(targetConfig.f_OutFile)

        if (targetConfig.parsed_commandline["toolsonly"] and targetConfig.parsed_commandline["g"] == None):
            print("Invalid option toolsonly with no output folder.")
            print("Must use the -g option with toolsonly option")
            AbnormalTermination("s_ERROR_OPTION_VALUE_INVALID - toolsonly option without g option", nverror.NvError_BadParameter)

        if(targetConfig.p_fp_platcfg == None ):
            print("\nINVALID PLATFORM COFIG\n")
            print("No valid platform config file was specified")
            AbnormalTermination("s_ERROR_OPTION_VALUE_INVALID - no valid platform config", nverror.NvError_BadParameter)

        #if (targetConfig.NV_BOOTBURN_SECURE_CHECK):
        #    if (targetConfig.n_SecurebootFlash and targetConfig.n_EngSample):
        #        print("Secure flashing is not supported on internal targets.\n")
        #        AbnormalTermination("s_ERROR_OPTION_VALUE_INVALID -- no secure boot on internal board", nverror.NvError_NotSupported)

        if (n_RcmMode):
            if (targetConfig.n_UFSProvisioningEnable):
                print("UFSProvisioning (-U) option not available with RCM or FAMode \n")
                AbnormalTermination ("s_ERROR_OPTION_CONFLICT -- -U not availible on RCM/FAMode", nverror.NvError_BadParameter)
            if (targetConfig.s_UpdatePartitions):
                print("Update partition(-u) option not avaliable with RCM or FAMode \n")
                AbnormalTermination ("s_ERROR_OPTION_CONFLICT -- -u not availible on RCM/FAMode", nverror.NvError_BadParameter)
            if (n_SkipFlashingRecoveryParts):
                print("Skip flashing recovery partitions(-o) option not avaliable with RCM or FAMode \n")
                AbnormalTermination("s_ERROR_OPTION_CONFLICT -- -o not availible on RCM/FAMode", nverror.NvError_BadParameter)
        else:
            if (targetConfig.n_GenBinsOnly):
                if (targetConfig.parsed_commandline["P"] != None):
                    print("-g and -P are mutually exclusive\n")
                    AbnormalTermination("s_ERROR_OPTION_CONFLICT -- -g and -P are mutually exclusive", nverror.NvError_BadParameter)
                if (targetConfig.n_ChipVersionSet == 0 and not targetConfig.parsed_commandline["toolsonly"]):
                    print("ChipVersion not set\n")
                    print(" -g option needs -r <ChipRevision> set\n")
                    AbnormalTermination("s_ERROR_TARGET_CHIP_VERSION -- -g option needs -r", nverror.NvError_BadParameter)
                if (targetConfig.parsed_commandline["o"] == True):
                    print("-g and -o are mutually exclusive\n")
                    AbnormalTermination("s_ERROR_OPTION_CONFLICT -- -g and -o are mutually exclusive", nverror.NvError_BadParameter)

        if (targetConfig.s_UpdatePartitions):
            if (n_SkipFlashingRecoveryParts):
                print("-u and -o are mutually exclusive\n")
                AbnormalTermination("s_ERROR_OPTION_CONFLICT -- -u and -o are mutually exclusive", nverror.NvError_BadParameter)

        if (targetConfig.s_BootDevice != "qspi" and targetConfig.s_BootDevice != "sdmmc" and targetConfig.s_BootDevice != "ufs"):
            print(targetConfig.s_BootDevice + " boot device not supported\n")
            print("Only emmc ,qspi and ufs are supported boot devices\n")
            AbnormalTermination ("s_ERROR_OPTION_VALUE_INVALID -- boot device invalid", nverror.NvError_NotSupported)

        if (targetConfig.f_BpmpDtbName):
            if (targetConfig.f_BpmpDtbName[0] == "."):
                print("Use absolute path, relative path is not supported for specifying bpmp-fw dtb targetConfig.f_BpmpDtbName\n")
                AbnormalTermination("s_ERROR_FILE_IO -- Use absolute path", nverror.NvError_FileOperationFailed)
            elif (targetConfig.f_BpmpDtbName[0] == "~" or targetConfig.f_BpmpDtbName[0] == "/"):
                targetConfig.f_BpmpDtbNameAbs = targetConfig.f_BpmpDtbName
                targetConfig.p_BpmpFwDtb = ""
                if(not os.path.isfile(targetConfig.f_BpmpDtbNameAbs)):
                    print("BPMP DTB NAME ABS: BpmpDtb " + targetConfig.f_BpmpDtbNameAbs + " does not exist\n")
                    AbnormalTermination ("s_ERROR_FILE_NOT_EXIST - " + targetConfig.f_BpmpDtbNameAbs, nverror.NvError_FileNotFound)
            elif (not os.path.isfile(os.path.join(targetConfig.p_BpmpFwDtb, targetConfig.f_BpmpDtbName))):
                print("BPMP FW DTB/BPMP DTB NAME: BpmpDtbName -- " + os.path.join(targetConfig.p_BpmpFwDtb, targetConfig.f_BpmpDtbName) + " -- does not exist\n")
                AbnormalTermination ("_ERROR_FILE_NOT_EXIST -- " + targetConfig.p_BpmpFwDtb + " / " + targetConfig.f_BpmpDtbName, nverror.NvError_FileNotFound)
        if (targetConfig.f_StorageDtbName):
            if (targetConfig.f_StorageDtbName[0] == "."):
                print("Use absolute path, relative path is not supported for specifying kernel-dtb targetConfig.f_StorageDtbName\n")
                AbnormalTermination ("s_ERROR_FILE_IO -- Absolute path required for dtb", nverror.NvError_FileOperationFailed)
            elif (targetConfig.f_StorageDtbName[0] == "~" or targetConfig.f_StorageDtbName[0] == "/"):
                targetConfig.f_StorageDtbNameAbs = os.path.abspath(targetConfig.f_StorageDtbName)
                targetConfig.p_StorageDtbPath = ""
                if not os.path.isfile(targetConfig.f_StorageDtbNameAbs):
                    print("STORAGE DTB NAME ABS: StorageDtbNameAbs -- " + targetConfig.f_StorageDtbNameAbs + "does not exist\n")
                    AbnormalTermination ("s_ERROR_FILE_NOT_EXIST -- " + targetConfig.f_StorageDtbNameAbs, nverror.NvError_FileNotFound)
            elif not os.path.isfile(os.path.join(targetConfig.p_StorageDtbPath, targetConfig.f_StorageDtbName)):
                print("STORAGE DTB PATH/STORAGE DTB: StorageDtbName does not exist\n")
                AbnormalTermination ("s_ERROR_FILE_NOT_EXIST -- " + targetConfig.p_StorageDtbPath + " / " + targetConfig.f_StorageDtbName, nverror.NvError_FileNotFound)

        userHelperFile = targetConfig.parsed_commandline["board_config"]
        if(userHelperFile != None and os.path.isfile(userHelperFile[0]) == False):
            msg = "Invalid path to user defined bootburn_helper - " + targetConfig.parsed_commandline["board_config"][0]
            print(msg)
            AbnormalTermination(msg, nverror.NvError_FileNotFound)

        # DRAM ECC is supported only in e3550b01-t194* and above
        if (targetConfig.n_DRAMECCEnabled and \
             ((targetConfig.parseDefaultValues("n_DramOverride") == None) or \
              (targetConfig.parseDefaultValues("n_DramOverride") != str(True)) )):
            print("DRAM ECC is not supported for this board -- " + targetConfig.s_BoardName + "\n")
            AbnormalTermination ("s_ERROR_OPTION_VALUE_INVALID -- wrong board type for DRAM ECC", nverror.NvError_NotSupported)

        if (targetConfig.n_FlashHypervisor):

            if (targetConfig.f_CustomFlashCfg):
                configData = self.shellUtils.catFile(targetConfig.f_CustomFlashCfg)
                bctFound = False
                for line in configData.splitlines():
                    line = line.rstrip()
                    if(not line or line[0] == '#'):
                        continue
                    line = line.split('#')[0]
                    if(line.find("name=bct") != -1):
                        bctFound = True
                        break;

                if (bctFound == False):
                    print("Not a valid hypervisor top level config file")
                    AbnormalTermination("${s_ERROR_PARSE_CONFIG} -- must specify top level config file using -k option", nverror.NvError_ConfigVarNotFound)

                l_cfgFile = os.path.dirname(targetConfig.f_CustomFlashCfg)
                try:
                    l_cfgFile = os.readlink(l_cfgFile)
                except:
                    #not a symbolic link
                    pass

                l_HyperCfgPath = targetConfig.p_HyperVisorCfgsPath
                if (os.path.exists(l_HyperCfgPath) == False):
                    print("Hypervisor path config paths is not a valid :" + l_HyperCfgPath)
                    AbnormalTermination("s_ERROR_PARSE_CONFIG -- hypervisor configuration path is not valid", nverror.NvError_ConfigVarNotFound)

                if (os.path.samefile(l_cfgFile, l_HyperCfgPath) == False):
                    print("When using -k option with hypervisor builds the config file must come from the ")
                    print("output folder generated by bind partitions.  Bind partition folder is " + l_HyperCfgPath + "cmdline config folder is:" + l_cfgFile)
                    print("If you wanted to use a native configuration then one of the options -l, -q, -a, or -G should be  used")
                    AbnormalTermination("s_ERROR_PARSE_CONFIG -- config file must be generated by bind partitions with using -k option", nverror.NvError_ConfigVarNotFound)

            if (targetConfig.f_StorageDtbName and not targetConfig.n_Asymmetric):
                print("There is no supported for specifying kernel-dtb with Hypervisor\n")
                AbnormalTermination ("s_ERROR_OPTION_VALUE_INVALID -- Cannot specify dtb with Hypervisor", nverror.NvError_NotSupported)

        if (len(targetConfig.s_UpdatePartitions) == 0 or "create_bsp" not in sys.argv[0]) and len(targetConfig.f_refPvitFile) != 0:
            print("Reference PVIT partition data should be passed only for partial generation (-u option with create_bsp) \n")
            AbnormalTermination ("s_ERROR_OPTION_VALUE_INVALID -- target_pvit to be provided  only with partial  generation (with -u option with create_bsp)", nverror.NvError_NotSupported)

    def parsePlatformConfig(self, targetConfig):
        if(targetConfig.n_FlashHypervisor == False or targetConfig.p_fp_platcfg == None):
            #no platform config for native builds or Asymmetric mode
            return

        if(os.path.isfile(targetConfig.p_fp_platcfg) == False):
            if(targetConfig.parsed_commandline["toolsonly"] == False):
                print("Platform config path - " + targetConfig.p_fp_platcfg + " - does not exist\n")
                print("Platform config file not found -- not overiding values")

            return

        fid = open(targetConfig.p_fp_platcfg, "r")
        data = fid.read()
        fid.close()

        targetConfig.platformConfig = json.loads(data)

        #do command line overrides
        if("bootburn-flags" in targetConfig.platformConfig):
            extraFlags = targetConfig.platformConfig["bootburn-flags"]
            print ("   ***  found bootburn-flags in platform-config.json **** ")
            commandLine = {}
            for option in extraFlags:
                value = extraFlags[option]
                print (" ****  value in bootburn-flags :" + str(value) )
                option = option.replace('-', '')
                commandLine[option] = value
                self.parsed_commandline[option] = value

            self.setTargetConfigDataCmdLine(targetConfig, commandLine)
            targetConfig.parsed_commandline = self.parsed_commandline

    def checkSafety(self, targetConfig):
        if(targetConfig.NV_BUILD_CONFIGURATION_IS_SAFETY == True):
            self.parsed_commandline["safety"] = True
        elif(targetConfig.isPDK == True and targetConfig.BOOTBURN_PYTEST == False):
            if(os.path.isfile(os.path.join(targetConfig.PDK_TOP, targetConfig.NV_SDK_NAME_FOUNDATION, targetConfig.safetyFileSDK))):
                self.parsed_commandline["safety"] = True
                targetConfig.SAFETY_BUILD = True

        if(self.parsed_commandline["safety"] == False):
            return

        if (self.parsed_commandline["asymmetric"] == True) and ("generate_asym" in os.path.basename(sys.argv[0])):
            targetConfig.NV_BUILD_CONFIGURATION_IS_SAFETY = False
            self.parsed_commandline["safety"] = False
            targetConfig.SAFETY_BUILD = False
            if (targetConfig.isPDK == True):
                targetConfig.unSafe_MB2 = True
            return

        if(targetConfig.NV_BOOTBURN_SAFETY_ALL_OPTIONS):
            print("\033[01;31m ENVIRONMENTAL VARIABLE NV_BOOTBURN_SAFETY_ALL_OPTIONS SET\033[0m")
            print("\033[01;31m ALL OPTIONS ENABLED IN SAFETY BUILD!\033[0m")
            return

        #________________________________________________________
        #             FIXME - Temp solution for GVS
        #               Need updated scripts on GVS
        #________________________________________________________
        if(targetConfig.TEST_AUTOMATION):
            return
        #________________________________________________________

        for option in self.parsed_commandline:
            argument = self.parsed_commandline[option]

            if(option == 'B' and argument != None):
                if(argument[0] != "qspi"):
                    AbnormalTermination("In Safety build only QSPI is allowed", nverror.NvError_InvalidArgument)
            elif((type(argument) is bool and argument == True) or (type(argument) == list)):
                isSafetyOption = (option in self.safetyAllowedOptions)

                if(isSafetyOption == False):
                    AbnormalTermination("option " + str(option) + " not allowed in safety flash", nverror.NvError_InvalidArgument)
