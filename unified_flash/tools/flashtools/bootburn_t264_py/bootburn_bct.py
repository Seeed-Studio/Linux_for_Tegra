#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2022-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.


import os
import stat
import re
from abc import ABCMeta, abstractmethod
import json
import shutil
from collections import defaultdict

from flashtools_nverror import nverror, AbnormalTermination
from flash_utilities import shell_utilities
import subprocess
import sys

cfg_namelist = [["filename_int", "filename_es", "filename"], ["imagepath_int", "imagepath_es", "imagepath"]]

class StorageConfigParser(object):
    """ Storage config parser
    """
    def __init__(self, targetConfig):
        self.targetConfig = targetConfig
        self.cfgFileList = []

        # Global and chain specific partition dict
        # key: 'g' for global partitions
        # key: '<chain id>' for chain specific partitions
        self.partitionDict = defaultdict(lambda: [])
        #all levels of storage config are saved as nested dictionary
        #global, bootchain, guestconfig
        self.partAttrDict  = defaultdict(lambda: {})
        #maps partition chain_guest to generated guest partition
        # ex:A_1:['A_qspi_chain#custom', 'A_qspi_chain#qnx-gos0']
        self.guestMapDict  = defaultdict(lambda: [])

    def getPartAttr(self,  dictData, chain, guest, partName, attrName, guest_level_map):
        partFound = False
        retval = False
        attrValue = ""
        attrValList = []
        for key, value in dictData.items():
            if isinstance(value, dict):
                #skip if the key is not same as the requested chain
                # compare with A_, B_, C_
                if len(key) >= 3 and (key[1] == '_') and (chain != key[0]):
                    continue
                #chain no. matches, skip if guest num  is not matching.
                #guest partitions mapped into chain#guest config file name
                #partition prefix: A_1, A_2, B_1, B_2, C, C_1, C_2
                #  A_1 :  'A_qspi_chain#custom',  A_qspi_chain#qnx-gos0
                #  A_2 :  'A_qspi_chain#qnx-update'
                if len(guest_level_map) != 0 and not (key in guest_level_map):
                    continue
                retval, attrValue  = self.getPartAttr(value, chain, guest, partName, attrName, guest_level_map)
                if retval:
                    return retval, attrValue

            elif partName == key:
                    self.targetConfig.sysMonitor.log("partName found in getPartAttr: " + key)
                    partFound = True
                    attrValList = value
                    for attr in attrValList:
                        if (attrName in attr):
                            attrValue = attr.split('=')[1]
                            self.targetConfig.sysMonitor.log("\nattrvalue :: " + attrValue + "\n")
                            retval = True
                            return retval, attrValue

        return retval, attrValue

    #Current partition dictionary naming convention
    #  global; A_qspi_chain; A_qspi_chain#qnx_gos0
    #Based on the partition attribute of the guest
    # configuration, generate a mapping of chain_guest
    #  to guest partition name in the dictionary.
    #  since there could be more than one guest subconfig
    #  for a given guest id, the value is made a list
    #
    #Geneate a dictionary with key: chain_Guest
    # value is a list of Level3 dictionaries

    def generateGuestDict(self, dictData):
        partSubcfg = False
        for key, value in dictData.items():
            if isinstance(value, dict):
                if (key[1] == '_') and ('#' not in key):
                    for  key1, value1 in value.items():
                        partSubcfg = False
                        for attr in value1:
                            if "partition_attribute" in attr:
                                partAttr = attr.split('=')[1] #and 0x03
                            if ("sub_cfg_file" in attr):
                                partSubcfg = True

                        if (partSubcfg):
                            subLvl = (partAttr[-1:])
                            intsubLvl = int(subLvl)
                            self.guestMapDict[key[0] + "_" + str(intsubLvl)].append(key +'#' +  key1)

    def parseAttrDict(self, partName, attrName):
        ''' Find the attribute value of a given partition
             using guestMapDict and partAttrDict
            Get the partition Dictionary using the
             guestMapDict. Then get the attribute-value
             list for the given partition, to extract
             value of the attribute.

        Args:
            partName (str)  : partition Name
            attrName (str)  : attribute Name of the partition
            value (str)     : value of the attrName of the partName
        Returns:
            True or False
        '''
        guest_level_map = []
        if (partName[1] == '_' and partName[3] == '_'):
            for chain_guest, partList in self.guestMapDict.items():
                if (chain_guest == partName[:3]) :
                    guest_level_map = partList
                    break

        chain = ""
        guest = ""
        if (partName[1] == '_'):
            chain = partName[0]
            if (partName[3] == "_"):
                guest = partName[2]
                partName = partName[4:]
            else:
                partName = partName[2:]

        retval, value = self.getPartAttr(self.partAttrDict, chain, guest, partName, attrName,  guest_level_map)
        return retval, value


    def findPartAttribVal(self, partName, attrName):
        """Find the attribute value of the specified partition
           from the storage config data

        Args:
           partName (str)  : partition Name
           attrName (str)  : attribute Name of the partition
           value (str)     : value of the attrName of the partName

        Returns:
           TRUE or FALSE

        """
        self.generateGuestDict(self.partAttrDict)
        retval, value = self.parseAttrDict(partName, attrName)
        self.targetConfig.sysMonitor.log("\n\n partName - " + partName + "  attrName - " + attrName + " value - " + value + "\n \n")

        return retval, value

    def findFileName(self, configData, filename):
        """ Find the specified file name from the storage config data

        Args:
            configData (str): Storage configurationd data
            filename (str): File name to be searched from storage config data

        Returns:
            str: File path of the found file name
        """
        foundFileName = None
        configDataSplit=configData
        configDataSplit=configDataSplit.splitlines()
        for line in configDataSplit:
            line.strip()
            if(not line or line[0] == '#'):
                continue

            if(line.find(filename) != -1):
                line = line.split('=')[1]
                line = line.strip()

                foundFileName = line
                break

        return foundFileName

    def preprocessCfgData(self, configData):
        """ Pre-process storage config data
            - Replace all dynamic variables in the storage config file
            - Gather some metadata required for flashing

        Args:
            configData (str): Storage configuration data

        Returns:
            str: Modified storage configuration data
        """
        buildFlavor = self.targetConfig.BUILD_FLAVOR
        targetBoard = self.targetConfig.TARGET_BOARD
        variant = self.targetConfig.NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT
        chipFamily = self.targetConfig.GetChipFamily()
        soc = self.targetConfig.GetSocType()

        if (self.targetConfig.TEGRA_TOP):
            if(self.targetConfig.NV_OUTDIR == None):
                AbnormalTermination("Foundation out dir path NV_OUTDIR not set", nverror.NvError_DirOperationFailed)
            elif(os.path.isdir(self.targetConfig.NV_OUTDIR) == False):
                AbnormalTermination("Foundation out dir path error " + self.targetConfig.NV_OUTDIR, nverror.NvError_DirOperationFailed)

            configData = configData.replace("<TOP>/foundation", self.targetConfig.TEGRA_TOP)
            configData = configData.replace("<FOUNDATION_OUTDIR>", self.targetConfig.NV_OUTDIR)
            configData = configData.replace("<KERNELVARIANT>", self.targetConfig.NV_BUILD_KERNEL_VARIANT_PATH_UNIQUEFIER)
            [linuxOutDir, linuxTop, safetyDir] = self.targetConfig.getOutDir("linux", targetBoard, buildFlavor, variant)
            configData = configData.replace("<TOP>/linux", linuxTop)
            configData = configData.replace("<LINUX_OUTDIR>", linuxOutDir)
            configData = configData.replace("<LINUX_DTB>", self.targetConfig.p_LinuxStorageDtbPath)
            configData = configData.replace("<LINUX_DTB>", self.targetConfig.p_LinuxStorageGenericDtbPath)
            configData = configData.replace("<LINUX_SAFETY_OUTDIR>", safetyDir)

            [qnxOutDir, qnxTop, safetyDir] = self.targetConfig.getOutDir("qnx", targetBoard, buildFlavor, variant)
            configData = configData.replace("<TOP>/qnx/", qnxTop+'/')
            configData = configData.replace("<QNX_OUTDIR>", qnxOutDir)

            if (not self.targetConfig.TOP):
                self.targetConfig.TOP = self.targetConfig.TEGRA_TOP + "/.."

            if(self.targetConfig.TEST_AUTOMATION == True):
                if(chipFamily == "t264"):
                    configData = configData.replace("<TOP>", self.targetConfig.TEGRA_TOP)
                else:
                    configData = configData.replace("<TOP>", self.targetConfig.TEGRA_TOP + "/..")
            else:
                configData = configData.replace("<TOP>", self.targetConfig.TEGRA_TOP)

            configData = configData.replace("<BUILD_FLAVOR>", self.targetConfig.BUILD_FLAVOR)

        configData = configData.replace("<CHIP_FAMILY>", chipFamily)
        configData = configData.replace("<SOC>", soc)
        configData = configData.replace("<BOARD>", self.targetConfig.s_BoardName)

        # Select filename_int in case of EngSample and filename_es in case of Prod

        for row in cfg_namelist:
            if (self.targetConfig.n_EngSample):
                configData = configData.replace(row[0], row[2])
                configData = re.sub(row[1] + "=.*\n", "", configData)
            else:
                configData = configData.replace(row[1], row[2])
                configData = re.sub(row[0] + "=.*\n", "", configData)

        if(self.targetConfig.isPDK):
            configData = configData.replace("<PDK_TOP>", self.targetConfig.PDK_TOP)
            configData = configData.replace("<TOP>", self.targetConfig.PDK_TOP)
            configData = configData.replace("<TARGET_BOARD>", self.targetConfig.TARGET_BOARD)
            qnxOutDir = os.path.abspath(os.path.join(self.targetConfig.PDK_TOP, self.targetConfig.NV_SDK_NAME_QNX))
            configData = configData.replace("<QNX_OUTDIR>", qnxOutDir)

        if (targetBoard):
            configData = configData.replace("<TARGET_PLATFORM>", self.targetConfig.TARGET_BOARD)

        if (self.targetConfig.s_BoardName):
            configData = configData.replace("<TARGET_BOARD_VARIANT>", self.targetConfig.s_BoardName)

        if (self.targetConfig.boardDefaultPaths["s_Kernel_Dtb_Variant"]):
            configData = configData.replace("<DTB_VARIANT>", self.targetConfig.boardDefaultPaths["s_Kernel_Dtb_Variant"])

        if (self.targetConfig.KernelDtbList):
            for name in self.targetConfig.KernelDtbList:
                index = self.targetConfig.KernelDtbList.index(name)
                if (index == 0):
                    continue
                key = "s_Kernel_Dtb_Variant" + str(index)
                substitute = "<DTB_VARIANT" + str(index) + ">"
                configData = configData.replace(substitute, self.targetConfig.boardDefaultPaths[key])

        if ("s_ChainC_Kernel_Dtb_Variant" in self.targetConfig.boardDefaultPaths):
            configData = configData.replace("<DTB_CHAIN_C_VARIANT>", self.targetConfig.boardDefaultPaths["s_ChainC_Kernel_Dtb_Variant"])

        if ("s_BPMP_Dtb_Variant" in self.targetConfig.boardDefaultPaths):
            configData = configData.replace("<BPMP_DTB_VARIANT>", self.targetConfig.boardDefaultPaths["s_BPMP_Dtb_Variant"])

        if (self.targetConfig.BpmpFwDtbList):
            for name in self.targetConfig.BpmpFwDtbList:
                index = self.targetConfig.BpmpFwDtbList.index(name)
                if (index == 0):
                    continue
                key = "s_BPMP_Dtb_Variant" + str(index)
                substitute = "<BPMP_DTB_VARIANT" + str(index) + ">"
                configData = configData.replace(substitute, self.targetConfig.boardDefaultPaths[key])

        if (self.targetConfig.boardDefaultPaths["s_IST_Ict_Variant"]):
            configData = configData.replace("<IST_ICT_VARIANT>", self.targetConfig.boardDefaultPaths["s_IST_Ict_Variant"])

        if (self.targetConfig.s_HypConfig):
            configData = configData.replace("<HYP_CONFIG>", self.targetConfig.s_HypConfig)

        if (self.targetConfig.P4ROOT):
            configData = configData.replace("<P4ROOT>", self.targetConfig.P4ROOT)

        if(self.targetConfig.NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT):
            configData = configData.replace("<EMBEDDED_VARIANT>", self.targetConfig.NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT)
        else:
            configData = configData.replace("<EMBEDDED_VARIANT>", "none")

        if (self.targetConfig.SAFETY_BUILD):
            configData = configData.replace("<QB_SAFE_SUFFIX>", "_safe")
        else:
            configData = configData.replace("<QB_SAFE_SUFFIX>", "")

        if (self.targetConfig.n_Asymmetric == True):
            fskp_name = self.targetConfig.fskpBinName
        else:
            fskp_name = "fskp_t234.bin"
        configData = configData.replace("<FSKP_NAME>", fskp_name)

        if (self.targetConfig.n_EngSample):
            configData = configData.replace("<PROD_VARIABLE>", "dev")
            configData = configData.replace("<BPMP_DEV>", "_dev")
        else:
            configData = configData.replace("<PROD_VARIABLE>", "prod")
            configData = configData.replace("<BPMP_DEV>", "")

        skuInfoFile = self.targetConfig.f_skuInfoJson
        if (not os.path.exists(skuInfoFile)):
            err_msg = ("Can't find Sku Info File %s" % (skuInfoFile))
            AbnormalTermination(err_msg, nverror.NvError_FileNotFound)

        with open(skuInfoFile, "r") as skuInfo:
            skuInfoDict = json.load(skuInfo)

        for key in skuInfoDict:
            if self.targetConfig.boardDefaultPaths["s_Skuname"] in skuInfoDict[key]:
                self.targetConfig.s_SkuInfo = key
                break

        if self.targetConfig.s_SkuInfo == None:
            err_msg = ("Can't Find Soc from Skuname %s" % self.targetConfig.boardDefaultPaths["s_Skuname"])
            AbnormalTermination(err_msg, nverror.NvError_InvalidArgument)

        #Read skuMapping
        if(self.targetConfig.isminiPDK):
            bpmpInfoFile = os.path.basename(self.targetConfig.f_bpmpInfoJson)
            bpmpInfoFile = os.path.join(self.targetConfig.p_SocBoardConfigsPath, bpmpInfoFile)
        else:
            bpmpInfoFile = self.targetConfig.f_bpmpInfoJson

        if (not os.path.exists(bpmpInfoFile)):
            err_msg = ("Can't find Bpmp Info File %s" % (bpmpInfoFile))
            AbnormalTermination(err_msg, nverror.NvError_FileNotFound)

        with open(bpmpInfoFile, "r") as bpmpInfo:
            bpmpInfoDict = json.load(bpmpInfo)

        s_BpmpName = None
        for key in bpmpInfoDict:
            if self.targetConfig.s_SkuInfo in bpmpInfoDict[key]:
                s_BpmpName = key
                break

        if s_BpmpName == None:
            err_msg = ("Can't Find Soc %s Bpmp File" % (self.targetConfig.s_SkuInfo))
            AbnormalTermination(err_msg, nverror.NvError_InvalidArgument)

        configData = configData.replace("<SKU>", s_BpmpName)

        partitionList = re.findall(r"\nname=(.*)\n", configData)

        if (configData.find("ufshci") != -1):
            self.targetConfig.n_ufshci = True

        dceFw = False
        for partition in partitionList:
            if ("dce-fw" in partition):
                dceFw = True
                break

        if (dceFw == True):
            self.targetConfig.s_dceName = self.findFileName(configData, "dce.bin")
            if (self.targetConfig.s_dceName == None):
               self.targetConfig.s_dceName = self.findFileName(configData, "dce-dispserver.bin")

        if (dceFw == True):
            self.targetConfig.s_dceDtbName = self.findFileName(configData, "filename_dtb")

        istConfig = False
        for partition in partitionList:
            if ("ist-config" in partition):
                istConfig = True
                break

        if (istConfig == True):
            self.targetConfig.p_NvICTBinPath = self.findFileName(configData, self.targetConfig.f_NvICTBinImage)

        istBpmp = False
        for partition in partitionList:
            if ("bpmp-ist" in partition):
                istBpmp = True
                break

        if (istBpmp == True):
            self.targetConfig.p_NvISTBinPath = self.findFileName(configData, self.targetConfig.f_NvISTBinImage)
            self.targetConfig.f_NvISTBinImage = os.path.basename(self.targetConfig.p_NvISTBinPath)

        return configData

    def parseConfigFile(self, configFile, newConfigFile, cfgFileKey="global"):
        """ Parse storage config and all sub config file to generate
            partition Dictionary and a dictionary of  partitions with
            their attributes

        Args:
            configFile (str): Path to storage config file
            newConfigFile (str): New storage config file path after parsing
            cfgFileKey (str): Either "global" for global config file or
                              partition name for sub config file
        """
        partStart = False
        partNameDict = {}
        if(os.path.isfile(configFile) is False):
            AbnormalTermination("Config file not found -- " + configFile, nverror.NvError_FileNotFound)

        self.cfgFileList.append([cfgFileKey, newConfigFile])

        baseConfigDir = os.path.dirname(configFile)
        newConfigDir = os.path.dirname(newConfigFile)
        baseName = os.path.splitext(os.path.basename(newConfigFile))[0]

        with open(configFile, 'r') as fid:
            configData = fid.read()

        configData = self.preprocessCfgData(configData)

        configData = configData.splitlines()

        fileKey = cfgFileKey
        with open(newConfigFile, 'a+', 0o666) as fid:
            i = 0
            for line in configData:
                line.strip()
                if(not line or line[0] == '#'):
                    fid.write(line + "\n")
                    continue

                if (line.startswith("[partition]")):
                    partStart = True
                    partNamefound = False
                elif (line.startswith("[")):
                    partStart = False
                    partNameFound = False
                if (partStart):
                    if (line.startswith("name")):
                        partitionName = line.split('=')[1]
                        partitionName = partitionName.strip()
                        partNamefound = True
                        # Update partition dict
                        self.partitionDict[fileKey[0]].append(partitionName)
                        partNameDict[partitionName] = []
                        self.partAttrDict[fileKey] = partNameDict

                    elif partNamefound:
                        self.partAttrDict[fileKey][partitionName].append(line.strip())
                        if(line.find("sub_cfg_file") != -1):
                            # prepend paritionName to the key
                            if (fileKey == "global"):
                                fileKey = partitionName
                            else:
                                fileKey = fileKey + "#" + partitionName

                            i += 1
                            line = line.split('=')[1]
                            line = line.strip()

                            subConfigDir = os.path.dirname(line)

                            if(not subConfigDir):
                                line = os.path.join(baseConfigDir, line)

                            newCfgFileName = os.path.join(newConfigDir, baseName + "_sub" + str(i) + ".cfg")
                            self.parseConfigFile(line, newCfgFileName, fileKey)
                            line = "sub_cfg_file=" + newCfgFileName

                            # Remove the last part after processing
                            fileKeyParts = fileKey.split("#")[:-1]
                            fileKey = "#".join(fileKeyParts)

                            if (fileKey == ''):
                                fileKey = "global"

                fid.write(line + "\n")

        os.chmod(newConfigFile, stat.S_IREAD | stat.S_IWRITE | stat.S_IRGRP | stat.S_IWGRP | stat.S_IROTH | stat.S_IWOTH)

class BctDefines(object):
    """ Abstract base class for BCT DTS defines
    """

    __metaclass__ = ABCMeta

    def __init__(self, targetConfig, storagePartitionList):
        self.targetConfig = targetConfig
        self.storagePartitionList = storagePartitionList
        self._definesList = []

        # Initialize partition name to defines map
        self.partitionToDefineMap = {
            "xusb-fw": "ENABLE_XUSB_FW",
            "tsec-fw": "ENABLE_TSEC",
            "nvdec-fw": "ENABLE_NVDEC",
            "fsi-fw": "ENABLE_FSI",
            "rce-fw": "ENABLE_RCE",
            "adsp-fw": "ENABLE_APE",
            "dce-fw": "ENABLE_DCE",
            "oist-tst-vtr": "ENABLE_RUNTIME_IST",
            "sc7-fw": "CONFIG_ENABLE_SC7",
            "pva-fw": "ENABLE_PVA",
        }

    def __getChainSpecificDefines(self, chain = 'A'):
        """Get Chain specific defines

        Args:
            chain (str, optional): Chain id. Defaults to 'A'.
        """

        # Define for BR BCT marker for a given chain
        activeMarkerDefine = "ACTIVE_CHAIN_MARKER=" + str(ord(chain) - ord('A'))

        self._definesList.append(activeMarkerDefine)
        self._definesList.append("ENABLE_MARKER")

    def getDefines(self, chain="A"):
        """ Get DTS defines for preprocessing DTS files using CPP

        Returns:
            list: List of BCT defines for DTS config file preprocessing
        """
        # Get common defines
        self.__getCommonDefines()

        # Get operation specific defines
        self._getOperationSpecificDefines()

        # Get chain specific defines
        self.__getChainSpecificDefines(chain)

        return self._definesList[:]

    def __getCommonDefines(self):
        """ Private method to get common defines for all operations
        """
        self._definesList.append("AUTO_BUILD")
        self._definesList.append("IN_DTS_CONTEXT")
        if (self.targetConfig.n_EngSample):
            self._definesList.append("BR_BCT_ENG_OVERRIDE")

        self.__getPartitionBasedDefines()

    @abstractmethod
    def _getOperationSpecificDefines(self):
        """ Get operation specific defines.
           Must be inherited by each operation
        """
        raise AbnormalTermination("Can't call abstractmethod!", nverror.NvError_InvalidState)

    def __getPartitionBasedDefines(self):
        """ Get defines based on list of partitions in the storage
            configuration file
        """
        for partition in self.storagePartitionList:
            if (partition in self.partitionToDefineMap):
                self._definesList.append(self.partitionToDefineMap[partition])

        if (self.targetConfig.n_ufshci):
            self._definesList.append("ENABLE_MB2_UFSHCI")

class RcmBootBctDefines(BctDefines):
    """ Class for RCM Boot defines
    """
    def __init__(self, targetConfig, storagePartitionList):
        super(RcmBootBctDefines, self).__init__(targetConfig, storagePartitionList)

    def _getOperationSpecificDefines(self):
        """ Gather RCM boot specific defines
        """

        if (self.targetConfig.n_FlashHypervisor):
            self._definesList.append("VIRT_BOOT_BCT")

        if (self.targetConfig.b_DisableUARTinMB1nMB2Bct == True):
            self._definesList.append("DISABLE_UART_MB1_MB2")

        if (self.targetConfig.parsed_commandline["m"]):
            self._definesList.append("DISABLE_PREPROD_FIREWALLS")
        elif (self.targetConfig.SAFETY_BUILD):
           self._definesList.append("ENABLE_SAFETY_SCR")

class FlashImagesBctDefines(BctDefines):
    """ Class for flash-images defines
    """
    def __init__(self, targetConfig, storagePartitionList):
        super(FlashImagesBctDefines, self).__init__(targetConfig, storagePartitionList)

    def _getOperationSpecificDefines(self):
        """ Gather flash-images specific defines
        """

        if (self.targetConfig.n_FlashHypervisor):
            self._definesList.append("VIRT_BOOT_BCT")

        if (self.targetConfig.n_DRAMECCEnabled):
            self._definesList.append("ENABLE_ECC")

        if (self.targetConfig.b_DisableUARTinMB1nMB2Bct == True):
            self._definesList.append("DISABLE_UART_MB1_MB2")

        if (self.targetConfig.parsed_commandline["m"]):
            self._definesList.append("DISABLE_PREPROD_FIREWALLS")
        elif (self.targetConfig.SAFETY_BUILD):
            self._definesList.append("ENABLE_SAFETY_SCR")

        self._definesList.append("ENABLE_SECURE_NOR_PROVISIONING")

class RcmFlashBctDefines(BctDefines):
    """ Class for flash-images defines
    """
    def __init__(self, targetConfig, storagePartitionList):
        super(RcmFlashBctDefines, self).__init__(targetConfig, storagePartitionList)

    def _getOperationSpecificDefines(self):
        """ Gather rcm-flash specific defines
        """
        self._definesList.append("ENABLE_FLASHING_SCR")

        if (self.targetConfig.platformConfig):
            # Add autogen flag
            if("INVOKE_AUTOGEN" in self.targetConfig.platformConfig["bct_defines"]["common"]):
                self._definesList.append("INVOKE_AUTOGEN")

            if ('ENABLE_MULTI_SKU_SUPPORT' in self.targetConfig.platformConfig["bct_defines"]["common"]):
                self._definesList.append('ENABLE_MULTI_SKU_SUPPORT')

class DefinesPolicy(object):
    """ Abstract policy base class for BCT DTS defines policies
    """

    __metaclass__ = ABCMeta

    @abstractmethod
    def getDefines(self, operation, targetConfig, storagePartitionList, chain="chain_A"):
        """ Get defines for specific operation

        Args:
            operation (str): Operation type (flash-images, rcm-boot or rcm-flash)
            targetConfig (TargetConfig): Target configuration object
            storagePartitionList (list): List of storage config partitions
            chain (str, optional): Chain type. Defaults to "chain_A".
        """
        raise AbnormalTermination("Method must be inherited by specific policy class ", nverror.NvError_InvalidState)

    @abstractmethod
    def getNumChains(self, targetConfig):
        """ Return number of chains

        Args:
            targetConfig (TargetConfig): Target configuration object
        """
        raise AbnormalTermination("Method must be inherited by specific policy class ", nverror.NvError_InvalidState)

class LegacyDefines(DefinesPolicy):
    """ Legacy defines policy class
        Get the defines hardcoded in bootburn code
    """
    def __init__(self):
        super(LegacyDefines, self).__init__()

    def getDefines(self, operation, targetConfig, storagePartitionList, chain="chain_A"):
        if ('rcm-flash' in operation):
            definesOb = RcmFlashBctDefines(targetConfig, storagePartitionList)
            return definesOb.getDefines(chain)
        elif (operation == 'rcm-boot'):
            definesOb = RcmBootBctDefines(targetConfig, storagePartitionList)
            return definesOb.getDefines(chain)
        elif (operation == 'flash-images'):
            definesOb = FlashImagesBctDefines(targetConfig, storagePartitionList)
            return definesOb.getDefines(chain)
        else:
            raise AbnormalTermination("Invalid operation argument: %s" % operation, nverror.NvError_InvalidArgument)

    def getNumChains(self, targetConfig):
        return 1

class PlatformJsonDefines(DefinesPolicy):
    """ Platform config defines policy
        Get the defines from platform config json file
    """
    def __init__(self):
        super(PlatformJsonDefines, self).__init__()

    def getNumChains(self, targetConfig):
        return int(targetConfig.platformConfig["bct_defines"]["number_of_chains"])

    def getDefines(self, operation, targetConfig, storagePartitionList, chain="A"):
        definesList = []
        definesList.append("AUTO_BUILD")
        definesList.append("IN_DTS_CONTEXT")
        if (targetConfig.n_EngSample):
            definesList.append("BR_BCT_ENG_OVERRIDE")

        if (targetConfig.parsed_commandline["m"]):
            definesList.append("DISABLE_PREPROD_FIREWALLS")
        elif (targetConfig.SAFETY_BUILD):
            definesList.append("ENABLE_SAFETY_SCR")

        if ('rcm-flash' in operation):
            definesOb = RcmFlashBctDefines(targetConfig, storagePartitionList)
            return definesOb.getDefines(chain)
        elif (operation == 'rcm-boot'):
            definesList.extend(targetConfig.platformConfig["bct_defines"]["common"][:])
            definesList.extend(targetConfig.platformConfig["bct_defines"]["rcm_boot"])
            return definesList
        elif (operation == 'flash-images'):
            definesList.extend(targetConfig.platformConfig["bct_defines"]["common"][:])
            definesList.extend(targetConfig.platformConfig["bct_defines"]["chains"]["chain_" + chain])
            activeMarkerDefine = "ACTIVE_CHAIN_MARKER=" + str(ord(chain) - ord('A'))
            definesList.append(activeMarkerDefine)
            definesList.append("ENABLE_MARKER")
            return definesList
        else:
            raise AbnormalTermination("Invalid operation argument: %s" % operation, nverror.NvError_InvalidArgument)

class BootburnBct(object):
    """ Abstract base class for BCT generation
    """

    __metaclass__ = ABCMeta

    def __init__(self, targetConfig, outputDir):
        self.dtsToTegraBctArgMap = { }
        self.tegraBctArgMap = {
            "--chip": str(targetConfig.GetChipID())
        }
        self.targetConfig = targetConfig
        self.tegrabctPath = targetConfig.flashUtils.f_TegraBct
        self.shellUtils = shell_utilities()
        self.outputDir = outputDir

    @abstractmethod
    def getBctFileName(self):
        raise AbnormalTermination("Can't call abstract method!", nverror.NvError_InvalidState)

    @classmethod
    def getBctObject(self, bctType, targetConfig, outputDir):
        """ Factory method to get bct type sepcific object

        Args:
            bctType (str): BCT type
            targetConfig (TargetConfig): Target configuration object
            outputDir (str): Path to output directory where output BCT
                             needs to be saved

        Raises:
            AbnormalTermination: Raises exception if invalid bct type
                                 is specified

        Returns:
            BootburnBct: Type specific BootburnBCT object based on type of BCT
        """
        if (bctType == "br_bct"):
            return BootburnBrBct(targetConfig, outputDir)
        elif (bctType == "hpse_bct"):
            return BootburnHpseSbBct(targetConfig, outputDir, 'HPCT')
        elif (bctType == "sb_bct"):
            return BootburnHpseSbBct(targetConfig, outputDir, 'SBCT')
        elif (bctType == "br_bct_asymmetric"):
            return BootburnAsymmetricMarkerBrBct(targetConfig, outputDir)
        elif (bctType == "br_bct_asymmetric_default"):
            return BootburnAsymmetricDefault(targetConfig, outputDir)
        elif (bctType == "mb1_bct"):
            return BootburnMb1Bct(targetConfig, outputDir)
        elif (bctType == "mb2_bct"):
            return BootburnMb2Bct(targetConfig, outputDir)
        elif (bctType == "mem_bct"):
            return BootburnMemBct(targetConfig, outputDir)
        elif (bctType == "bpmp_mem_bin"):
            return BootburnBpmpMemBct(targetConfig, outputDir)
        elif (bctType == "mb1_multi_sku_platform_bct"):
            return BootburnMb1MultiSkuBct(targetConfig, outputDir)
        else:
            raise AbnormalTermination("Unknown BCT type specified!", nverror.NvError_InvalidArgument)

    def __getBctDtbIncludePaths(self):
        """Get the DTS include paths for preprocessing
        """
        jsonIncludesFile = os.path.join(self.targetConfig.p_DtsIncludesJsonPath, "dts_includes.json")
        bctDtsIncludeDirs = []
        if(os.path.isfile(jsonIncludesFile)):
            with open(jsonIncludesFile) as f:
                data = f.read()
            dtsIncludes = json.loads(data)['dts_includes']
            for inc in dtsIncludes:
                inc = self.targetConfig.parseDefaultValues(str(inc), True)
                bctDtsIncludeDirs.append(inc)

        return bctDtsIncludeDirs

    def generateBct(self, bctDefines):
        """ Generate BCT based on BCT defines using DTS files from
           platform Json file

        Args:
            bctDefines (list): List of BCT defines for DTS preprocessing
        """
        # Prerprocess dts files via CPP
        dtsFileList = []
        for dtsFile in self.dtsToTegraBctArgMap.values():
            if dtsFile and ".dts" in dtsFile and os.path.exists(dtsFile):
                dtsFileList.append(dtsFile)

        # Get include paths for dts preprocessing
        includePaths = self.__getBctDtbIncludePaths()

        # Add autogen carveouts path to include paths if INVOKE_AUTOGEN is in bctDefines
        # We are not adding this path by default because this path will only be generated when autogen is enabled during bind partitions
        if("INVOKE_AUTOGEN" in bctDefines):
            autogenMb1BctCarveoutsPath = os.path.join(self.targetConfig.p_autogenPath, "carveouts")
            includePaths.append(autogenMb1BctCarveoutsPath)

        # Copy DTS files to output directory before processing
        destDtsFileList = []
        includePathList = []
        for srcDtsFilePath in dtsFileList:
            dtsFileBaseName = os.path.basename(srcDtsFilePath)
            dstDtsFilePath = os.path.join(self.outputDir, dtsFileBaseName)
            shutil.copyfile(srcDtsFilePath, dstDtsFilePath)
            destDtsFileList.append(dtsFileBaseName)
            includePathList.append(includePaths[:])
            includePathList[-1].extend([os.path.abspath(os.path.dirname(srcDtsFilePath))])

        outFileList = self.__runCppTool(bctDefines, destDtsFileList, includePathList)

        # Process dts files through DTC
        outFileList = self.__runDtcTool(outFileList)

        # Execute tegrabct_v2 to generate BCT
        tegrabctCmd = [self.tegrabctPath]
        for key, value in self.tegraBctArgMap.items():
            tegrabctCmd.extend([key, value])

        # Special case where wb0 dts processing needs to be removed
        # if SC7 partition is not present, remove wb0 param from the dictionary
        if ("CONFIG_ENABLE_SC7" not in bctDefines and "--wb0sdram" in self.dtsToTegraBctArgMap):
            self.dtsToTegraBctArgMap.pop("--wb0sdram")

        for tegrabctArg, dtsFile in self.dtsToTegraBctArgMap.items():
            # Replace .dts to .dtb in the path because .dtb is the final
            # output from processing dts file to dtb
            if (dtsFile):
                srcDtsFileName = os.path.basename(dtsFile)
                dstDtbFilePath = os.path.join(self.outputDir, srcDtsFileName)
                fileName, _ = os.path.splitext(dstDtbFilePath)
                dstDtbFilePath = fileName + "_cpp.dtb"
                if (os.path.exists(dstDtbFilePath)):
                    tegrabctCmd.extend([tegrabctArg, dstDtbFilePath])

        self.__runTegraBct(tegrabctCmd)

    def fillPcpPcpDigests(self, pcpKeyListFile):
        """ Execute tegrabct_v2 to fill pcp and pcp digests of BCT
            example: tegrabct_v2 --brbct <bct_file> --chip 0x26 0 --pubkeyhash <xml file>
        Args:
            pcpKeyListFile: an xml file that contains key list to be processed and udpated
            for bct's pcp, active index, and pcp_digests
            if pcpKeyListFile is none, then return since this means oem-sign is disabled
        """
        if pcpKeyListFile == None:
            return
        targetArgs = ["--brbct", "--chip", "--pubkeyhash"]
        tegrabctCmd = [self.tegrabctPath]
        tegrabctCmd.extend(["--brbct", self.getBctFileName()])
        tegrabctCmd.extend(["--chip", "0x26" , "0"])
        tegrabctCmd.extend(["--pubkeyhash", pcpKeyListFile])

        self.__runTegraBct(tegrabctCmd)

    def __runCppTool(self, bctDefines, inputFileList, includePathList):
        """ Run cpp tool on input DTS file and produce preprocessed DTS file

        Args:
            bctDefines (list): List of C defines for cpp tool
            inputFileList (list): List of dts files to compile in parallel
            includePathList (list(list)): List of list of include paths
                                          Each list is for specific DTS file
        """

        cppCmdList = []
        outFileList = []
        for inputFile, pathList in zip(inputFileList, includePathList):
            if(os.path.exists(inputFile) == False):
                AbnormalTermination("File not found\n" + inputFile, nverror.NvError_FileNotFound)

            self.targetConfig.sysMonitor.log("Preprocessing DTS file: %s " % inputFile)
            if ("board_specific_includes" in self.targetConfig.boardDefaultPaths):
                for path in self.targetConfig.boardDefaultPaths["board_specific_includes"]:
                    path = self.targetConfig.parseDefaultValues(path, True)
                    pathList.append(path)
            # Construct include file string
            includePaths = " ".join(["-I" + incDir for incDir in pathList])

            # Construct macro variable strings to be passed to cpp
            macroString = ""
            for flag in bctDefines:
                macroString += " -D "
                macroString += flag
                macroString += " "

            file_name, _ = os.path.splitext(inputFile)
            outFile = file_name + "_cpp.dts"
            # run cpp tool to process any c header includes in DTS
            cppCommand  = "cpp" + " -nostdinc " + "-x assembler-with-cpp " + macroString

            cppCommand += includePaths + " "
            cppCommand += " -o " + outFile
            cppCommand += "  " + inputFile

            cppCmdList.append(cppCommand)

            outFileList.append(outFile)

        result = self.shellUtils.executeParallelShellCommands(cppCmdList)
        if(result != nverror.NvError_Success):
            self.targetConfig.sysMonitor.log("cppCmdList:" + "\n".join(cppCmdList))
            AbnormalTermination("CPP command failed", nverror.NvError_SystemCommand)

        return outFileList

    def __runDtcTool(self, dtsFileList):
        """ Run DTC tool in parallel to convert list of DTS files to DTB files

        Args:
            dtsFileList (list): List of DTS files

        Returns:
            list: List of output DTB files
        """
        # running dtc in  quiet, make l_nodtcDump an empty string to enable dtc log
        l_nodtcDump = " -qqq "
        outDtbFileList = []
        dtcCmdList = []
        for dtsFile in dtsFileList:
            self.targetConfig.sysMonitor.log("Compiling DTS file: %s" % dtsFile)

            dtsFileBaseName = os.path.basename(dtsFile)
            fileNameParts = dtsFileBaseName.split('.')

            dtbFileName = fileNameParts[0] + ".dtb"

            dtbFilePath = os.path.join(self.outputDir, dtbFileName)

            # run dtc to compile DTS into dtb
            dtcCommand = self.targetConfig.f_DTCTool + " -I " + " dts " + " -O " + " dtb "
            dtcCommand += " -o " + dtbFilePath + " " + l_nodtcDump
            dtcCommand += dtsFile
            dtcCmdList.append(dtcCommand)

            outDtbFileList.append(dtbFilePath)

        result = self.shellUtils.executeParallelShellCommands(dtcCmdList)

        if(result != nverror.NvError_Success):
            AbnormalTermination("DTC command failed", nverror.NvError_SystemCommand)

        return outDtbFileList

    def __runTegraBct(self, command):
        """ Run tegrabct command

        Args:
            command (list): tegrabct command list
        """
        self.targetConfig.sysMonitor.log("Running tegrabct command: %s" % " ".join(command))
        result = self.shellUtils.executeShellCommand(" ".join(command))
        if (result != nverror.NvError_Success):
            AbnormalTermination("s_ERROR_TOOL_TEGRABCT", nverror.NvError_TegraBctError)

    def deleteFiles(self):
        """ Delete generated BCT files
            This method is inteded to be called after BCT processing
            is done
        """
        fileList = []
        for fileName in self.dtsToTegraBctArgMap.values():
            if fileName and ".dts" in fileName and os.path.exists(fileName):
                baseFileName = os.path.basename(fileName)
                name, ext = os.path.splitext(baseFileName)
                cppFileName = name + "_cpp.dts"
                dtbFileName = name + "_cpp.dtb"
                fileList.extend([baseFileName, cppFileName, dtbFileName])

        # Remove DTS/DTB Files only in non-debug
        if (self.targetConfig.s_debugOutput == False):
            for fileName in fileList:
                filePath = os.path.join(self.outputDir, fileName)
                if (os.path.exists(filePath)):
                    os.remove(filePath)

        # Remove BCT file(s)
        bctFilePath = os.path.join(self.outputDir, self.getBctFileName())
        if (os.path.exists(bctFilePath)):
            os.remove(bctFilePath)
        else:
            self.targetConfig.sysMonitor.log(
                "File %s doesn't exist, so couldn't delete it" % bctFilePath,
                True
            )

class BootburnHpseSbBct(BootburnBct):
    """ Class to generate HPSE BCT
    """
    def __init__(self, targetConfig, outputDir, magicid):
        super(BootburnHpseSbBct, self).__init__(targetConfig, outputDir)
        self.magicid = magicid

        if magicid == "HPCT":
            self.dtsToTegraBctArgMap = {
                "--dev_param" : self.targetConfig.boardDefaultPaths["f_HpseBrBctDevParam"],
                "--sdram" : self.targetConfig.boardDefaultPaths["f_HpseMB1SdRamParam"],
                "--wb0sdram" : self.targetConfig.boardDefaultPaths["f_HpseMB1wb0SdRamParam"],
            }
        elif magicid == "SBCT":
            self.dtsToTegraBctArgMap = {
                "--dev_param" : self.targetConfig.boardDefaultPaths["f_SbBrBctDevParam"],
                "--sdram" : self.targetConfig.boardDefaultPaths["f_SbMB1SdRamParam"],
                "--wb0sdram" : self.targetConfig.boardDefaultPaths["f_SbMB1wb0SdRamParam"],
            }
        else:
            raise AbnormalTermination("Invalid magic id: %s" % str(magicid), nverror.NvError_InvalidArgument)

        self.tegraBctArgMap["--magicid"] = magicid
        self.bctFileNameSuffix = magicid + "_bct.cfg"
        self.tegraBctArgMap["--brbct"] = self.bctFileNameSuffix

    def getBctFileName(self, magicid = None):
        return os.path.splitext(self.bctFileNameSuffix)[0] + "_BR.bct"

    def generateBct(self, bctDefines):
        hpseBctDefines = bctDefines[:]
        hpseBctDefines.append('MEM_BCT')
        return super(BootburnHpseSbBct, self).generateBct(hpseBctDefines)

class BootburnBrBct(BootburnBct):
    """ Class to generate BR BCT
    """
    def __init__(self, targetConfig, outputDir):
        super(BootburnBrBct, self).__init__(targetConfig, outputDir)

        self.dtsToTegraBctArgMap = {
            "--dev_param" : self.targetConfig.boardDefaultPaths["f_BrBctDevParam"],
            "--sdram" : self.targetConfig.boardDefaultPaths["f_MB1SdRamParam"],
            "--wb0sdram" : self.targetConfig.boardDefaultPaths["f_MB1wb0SdRamParam"],
        }

        self.bctFileNameSuffix = "bct.cfg"
        self.tegraBctArgMap["--brbct"] = self.bctFileNameSuffix

    def getBctFileName(self):
        return os.path.splitext(self.bctFileNameSuffix)[0] + "_BR.bct"

    def generateBct(self, bctDefines):
        brBctDefines = bctDefines[:]
        brBctDefines.append('MEM_BCT')
        return super(BootburnBrBct, self).generateBct(brBctDefines)

class BootburnAsymmetricMarkerBrBct(BootburnBrBct):
    """ Class to generate Asymmetric maker based BR BCT
    """

    def __init__(self, targetConfig, outputDir):
        super(BootburnAsymmetricMarkerBrBct, self).__init__(targetConfig, outputDir)

    def generateBct(self, bctDefines):
        # Disable GPIO flag to force marker based BCT
        bctDefines.append("DISABLE_GPIO_CHAIN_SELECT")

        return super(BootburnAsymmetricMarkerBrBct, self).generateBct(bctDefines)


class BootburnAsymmetricDefault(BootburnBrBct):
    """ Class to generate Asymmetric default BR BCT
    """

    def __init__(self, targetConfig, outputDir):
        super(BootburnAsymmetricDefault, self).__init__(targetConfig, outputDir)

        self.bctFileNameSuffix = "bct_asym_default.cfg"
        self.tegraBctArgMap["--brbct"] = self.bctFileNameSuffix


class BootburnMb1Bct(BootburnBct):
    """ Class to generate Mb1 BCT
    """
    def __init__(self, targetConfig, outputDir):
        super(BootburnMb1Bct, self).__init__(targetConfig, outputDir)

        self.dtsToTegraBctArgMap = {
            "--sdram" : self.targetConfig.boardDefaultPaths["f_MB1SdRamParam"],
            "--wb0sdram" : self.targetConfig.boardDefaultPaths["f_MB1wb0SdRamParam"],
            "--device" : self.targetConfig.boardDefaultPaths["f_MB1BootDevice"],
            "--uphy" :  self.targetConfig.boardDefaultPaths["f_UFSPhyLaneFile"],
            "--pinmux" : self.targetConfig.boardDefaultPaths["f_MB1Pinmux"],
            "--pmic" :  self.targetConfig.boardDefaultPaths["f_MB1Pmic"],
            "--pmc"  :  self.targetConfig.boardDefaultPaths["f_MB1Pad"],
            "--misc" :  self.targetConfig.boardDefaultPaths["f_MB1Misc"],
            "--prod" :  self.targetConfig.boardDefaultPaths["f_MB1Prod"],
            "--gpioint" : self.targetConfig.boardDefaultPaths["f_MB1GpioInt"],
            "--deviceprod" : self.targetConfig.boardDefaultPaths["f_MB1DeviceProd"],
            "--minratchet": self.targetConfig.boardDefaultPaths["f_MB1MinRatchet"]
        }

        self.tegraBctArgMap["--fb"] = os.path.join(self.outputDir, "fusebypass.bin")

        self.bctFileNameSuffix = "bct.cfg"
        self.tegraBctArgMap["--mb1bct"] = "bct.cfg"

    def getBctFileName(self):
        return os.path.splitext(self.bctFileNameSuffix)[0] + "_MB1.bct"

    def generateBct(self, bctDefines):
        mb1BctDefines = bctDefines[:]
        mb1BctDefines.append('MEM_BCT')
        super(BootburnMb1Bct, self).generateBct(mb1BctDefines)

        # Now the BCT is generated using dts files
        # Change the name of the binary for tegrabct command
        self.tegraBctArgMap["--mb1bct"] = self.getBctFileName()

        tegrabctCmd = [self.tegrabctPath]
        for key, value in self.tegraBctArgMap.items():
            tegrabctCmd.extend([key, value])

        # Update storage information in Mb1 BCT
        # MB1 has a small table for secondary storage devices with two items in in {type of device, instance}.
        # There can be up to 8 instances of 2-tuple
        if (self.targetConfig.s_BootDevice in ['qspi', 'ufs']):
            storage_devices = self.targetConfig.boardDefaultPaths["f_MB2StorageDevices"]
            if (storage_devices is not None):
                devicesMap = {
                    # bct define: (device instance, device type)
                    "ENABLE_MB2_UFSHCI": ("0", "ufs"),
                }

                cwd = os.getcwd()
                devices_xml = os.path.join(cwd, os.path.basename(storage_devices))
                with open(storage_devices, "r") as file_in, open(devices_xml, "w") as file_out:
                    input_data = file_in.read()
                    input_data = input_data.split("\n")

                    for line in input_data:
                        if "</partition_layout>" in line:
                            for define, deviceInfo in devicesMap.items():
                                if (define in bctDefines):
                                    file_out.write("    <device instance=\""+ deviceInfo[0] + "\" type=\"" + deviceInfo[1] + "\">\n    </device>\n")
                        file_out.write(line+"\n")

                storage_devices = os.path.splitext(os.path.basename(storage_devices))[0]
                tegraParsercommand = self.targetConfig.flashUtils.f_TegraParser + " --pt " + devices_xml
                result = self.shellUtils.executeShellCommand(tegraParsercommand)
                if (result != nverror.NvError_Success):
                    AbnormalTermination("s_ERROR_TOOL_TEGRAPARSER", nverror.NvError_TegraParserError)
                storage_devices_bin = os.path.join(cwd, storage_devices + ".bin")
                tegrabctCmd.extend(["--updatestorageinfo", storage_devices_bin])
            else:
                AbnormalTermination("f_MB1StorageDevices undefined", nverror.NvError_BadParamter)

        result = self.shellUtils.executeShellCommand(" ".join(tegrabctCmd))
        if (result != nverror.NvError_Success):
            AbnormalTermination("s_ERROR_TOOL_TEGRABCT", nverror.NvError_TegraBctError)


class BootburnMb2Bct(BootburnBct):
    """ Class to generate Mb2 BCT
    """
    def __init__(self, targetConfig, outputDir):
        super(BootburnMb2Bct, self).__init__(targetConfig, outputDir)

        self.dtsToTegraBctArgMap = {
            "--mb2bctcfg" : self.targetConfig.boardDefaultPaths["f_MB2Bct"],
            "--scr": self.targetConfig.boardDefaultPaths["f_MB2Scr"]
        }

        self.bctFileNameSuffix = "mb2bct.cfg"
        self.tegraBctArgMap["--mb2bct"] = self.bctFileNameSuffix
        self.tegraBctBinName = os.path.splitext(self.tegraBctArgMap["--mb2bct"])[0] + "_MB2.bct"

    def getBctFileName(self):
        return os.path.splitext(self.bctFileNameSuffix)[0] + "_MB2.bct"

    def generateBct(self, bctDefines):
        # Generate BCT based on dts files
        super(BootburnMb2Bct, self).generateBct(bctDefines)

        # Now the BCT is generated using dts files
        # Change the name of the binary for tegrabct command
        self.tegraBctArgMap["--mb2bct"] = self.tegraBctBinName

        tegrabctCmd = [self.tegrabctPath]
        for key, value in self.tegraBctArgMap.items():
            tegrabctCmd.extend([key, value])

        # Update storage information in Mb2 BCT
        # MB2 has a small table for secondary storage devices with two items in in {type of device, instance}.
        # There can be up to 8 instances of 2-tuple
        if (self.targetConfig.s_BootDevice in ['qspi', 'ufs']):
            storage_devices = self.targetConfig.boardDefaultPaths["f_MB2StorageDevices"]
            if (storage_devices is not None):
                devicesMap = {
                    # bct define: (device instance, device type)
                    "ENABLE_MB2_UFSHCI": ("0", "ufs"),
                }

                cwd = os.getcwd()
                devices_xml = os.path.join(cwd, os.path.basename(storage_devices))
                with open(storage_devices, "r") as file_in, open(devices_xml, "w") as file_out:
                    input_data = file_in.read()
                    input_data = input_data.split("\n")

                    for line in input_data:
                        if "</partition_layout>" in line:
                            for define, deviceInfo in devicesMap.items():
                                if (define in bctDefines):
                                    file_out.write("    <device instance=\""+ deviceInfo[0] + "\" type=\"" + deviceInfo[1] + "\">\n    </device>\n")
                        file_out.write(line+"\n")

                storage_devices = os.path.splitext(os.path.basename(storage_devices))[0]
                tegraParsercommand = self.targetConfig.flashUtils.f_TegraParser + " --pt " + devices_xml
                result = self.shellUtils.executeShellCommand(tegraParsercommand)
                if (result != nverror.NvError_Success):
                    AbnormalTermination("s_ERROR_TOOL_TEGRAPARSER", nverror.NvError_TegraParserError)
                storage_devices_bin = os.path.join(cwd, storage_devices + ".bin")
                tegrabctCmd.extend(["--updatestorageinfo", storage_devices_bin])
            else:
                AbnormalTermination("f_MB2StorageDevices undefined", nverror.NvError_BadParamter)

        result = self.shellUtils.executeShellCommand(" ".join(tegrabctCmd))
        if (result != nverror.NvError_Success):
            AbnormalTermination("s_ERROR_TOOL_TEGRABCT", nverror.NvError_TegraBctError)

class BootburnMemBct(BootburnBct):
    """ Class to generate MEM BCT
    """
    def __init__(self, targetConfig, outputDir):
        super(BootburnMemBct, self).__init__(targetConfig, outputDir)

        self.numMemBcts = 8

        self.dtsToTegraBctArgMap = {
            "--sdram" : self.targetConfig.boardDefaultPaths["f_MB1SdRamParam"],
            "--wb0sdram" : self.targetConfig.boardDefaultPaths["f_MB1wb0SdRamParam"],
        }

        self.tegraBctArgMap["--membct"] = " ".join([memBct for memBct in targetConfig.filelist["f_MembctBin"]])


    def generateBct(self, bctDefines):
        memBctDefines = bctDefines[:]
        memBctDefines.append('MEM_BCT')
        return super(BootburnMemBct, self).generateBct(memBctDefines)

    def getBctFileName(self):
        # For membct, there are always 4 BCTs so return all the name as a string
        # delimited by space
        memBctFileNameString = " ".join([memBct for memBct in self.targetConfig.filelist["f_MembctBin"]])

        return memBctFileNameString

    def deleteFiles(self):
        """ There are four Mem BCT files so delete
            all here
        """
        # Remove DTS/DTB Files only in non-debug
        if (self.targetConfig.s_debugOutput == False):
            for fileName in self.getBctFileName().split(" "):
                bctFilePath = os.path.join(self.outputDir, fileName)
                if (os.path.exists(bctFilePath)):
                    os.remove(bctFilePath)
                else:
                    self.targetConfig.sysMonitor.log(
                        "File %s doesn't exist, so couldn't delete it",
                        bctFilePath
                    )

class BootburnBpmpMemBct(BootburnBct):
    """ Class to generate BPMP MEM BCT
    """
    def __init__(self, targetConfig, outputDir):
        super(BootburnBpmpMemBct, self).__init__(targetConfig, outputDir)

        self.numMemBcts = 8

        if 'f_BpmpMemCfgParam' in self.targetConfig.boardDefaultPaths:
            self.dtsToTegraBctArgMap = {
                "--bpmp_mem_cfg" : self.targetConfig.boardDefaultPaths['f_BpmpMemCfgParam']
            }

        self.tegraBctArgMap["--bpmp_mem_bin"] = " ".join([bpmpMemBct for bpmpMemBct in targetConfig.filelist["f_BpmpMembctBin"]])

    def generateBct(self, bctDefines):
        bpmpBctDefines = bctDefines[:]
        bpmpBctDefines.append('BPMP_MEM_CFG')
        return super(BootburnBpmpMemBct, self).generateBct(bpmpBctDefines)

    def getBctFileName(self):
        bpmpmemBctFileNameString = " ".join([bpmpmemBct for bpmpmemBct in self.targetConfig.filelist["f_BpmpMembctBin"]])

        return bpmpmemBctFileNameString

    def deleteFiles(self):
        """ There are eight Bpmp Mem BCT files so delete
            all here
        """
        # Remove DTS/DTB Files only in non-debug
        if (self.targetConfig.s_debugOutput == False):
            for fileName in self.getBctFileName().split(" "):
                bctFilePath = os.path.join(self.outputDir, fileName)
                if (os.path.exists(bctFilePath)):
                    os.remove(bctFilePath)
                else:
                    self.targetConfig.sysMonitor.log(
                        "File %s doesn't exist, so couldn't delete it",
                        bctFilePath
                    )

class BootburnMb1MultiSkuBct(BootburnBct):
    """ Class to generate MB1 multi sku platform data BCT binaries
    """
    def __init__(self, targetConfig, outputDir):
        super(BootburnMb1MultiSkuBct, self).__init__(targetConfig, outputDir)

        if 'f_MB1MultiSkuPlatformParam' in self.targetConfig.boardDefaultPaths:
            self.dtsToTegraBctArgMap = {
                "--multi_sku_platform_cfg" : self.targetConfig.boardDefaultPaths['f_MB1MultiSkuPlatformParam']
            }

        self.tegraBctArgMap["--multi_sku_platform_data_bin"] = " ".join([multiskuBct for multiskuBct in targetConfig.filelist["f_Mb1MultiSkuPlatformBctBin"]])

    def getBctFileName(self):
        multi_sku_bct_filename_str = " ".join([multisku_bct for multisku_bct in self.targetConfig.filelist["f_Mb1MultiSkuPlatformBctBin"]])

        return multi_sku_bct_filename_str

    def deleteFiles(self):
        """ There are eight Bpmp Mem BCT files so delete
            all here
        """
        # Remove DTS/DTB Files only in non-debug
        if (self.targetConfig.s_debugOutput == False):
            for fileName in self.getBctFileName().split(" "):
                bctFilePath = os.path.join(self.outputDir, fileName)
                if (os.path.exists(bctFilePath)):
                    os.remove(bctFilePath)
                else:
                    self.targetConfig.sysMonitor.log(
                        "File %s doesn't exist, so couldn't delete it",
                        bctFilePath
                    )