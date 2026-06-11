#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2017-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

import os
import sys
import argparse
import traceback
from flash_utilities import flash_utilities
from flash_utilities import shell_utilities
from system_monitor import monitor
from flashtools_nverror import AbnormalTermination, nverror
import json
from customer_data_parser import CustomerDataProcessor

class CustomerData():
    """Class to help manage customer data
    """

    def __init__(self, dataValues = {}, sectionType = "customer-data-unsigned"):
        self.customerDataDict = dataValues
        self.sectionType = sectionType

    def updateValuesFrom(self, customerDataObject):
        for key, value in customerDataObject.customerDataDict.items():
            self.customerDataDict[key] = value

    def dumpToFile(self, fileName):
        with open(fileName, "w") as custDataFile:
            json.dump({self.sectionType: self.customerDataDict}, custDataFile)

    def dumpBothToFile(self, tc, fileName):
        with open(fileName, "w")  as custDataFile:
            json.dump({tc.signedCustomerData.sectionType:  tc.signedCustomerData.customerDataDict,
                       tc.customerData.sectionType:  tc.customerData.customerDataDict}, custDataFile)

    def setKeyValue(self, key, value):
        self.customerDataDict[key] = value

    def hasKey(self, key):
        return key in self.customerDataDict.keys()

    def getValue(self, key):
        if (key in self.customerDataDict.keys()):
            if (self.customerDataDict[key], dict):
                return self.customerDataDict[key]
            else:
                return str(self.customerDataDict[key])

        return None

    def isLoaded(self):
        return len(self.customerDataDict.items()) > 0

    def clear(self):
        self.customerDataDict.clear()

    def disp(self):
        print("%s" % self.sectionType)
        print(self.customerDataDict)

class target_config(object):

    sku_options = {
        "--skunum": (1, "sku id (mandatory with -z), e.g. 699-62382-0010-100", False),  # n_skuarg_skunum
        "--setskuversion": (1, "sku version (mandatory with -z), e.g. AB", False),  # n_skuarg_setskuversion
        "--setprodinfo": (2, "prod id, e.g. 699-62382-0010-100 AB", False),  # n_skuarg_setprodinfo
        "--setboardserial": (1, "serial id, e.g. 1234567890", False),  # n_skuarg_setboardserial
        "--setmacid": (2, "interface info, e.g. mac0 0xaabbccddeeff", False),  # n_skuarg_setmacid
        "--setmacid0": (2, "interface info, e.g. mac0 0xaabbccddeeff", False),  # n_skuarg_setmacid
        "--setmacid1": (2, "interface info, e.g. mac0 0xaabbccddeeff", False),  # n_skuarg_setmacid1
        "--setmacid2": (2, "interface info, e.g. mac0 0xaabbccddeeff", False),  # n_skuarg_setmacid2
        "--setmacid3": (2, "interface info, e.g. mac0 0xaabbccddeeff", False),  # n_skuarg_setmacid3
    }
    shellUtils = shell_utilities()
    s_Nvidia_USB_ID = "0955"
    s_Tegra_USB_Product_ID = "7100"
    flashUtils = flash_utilities
    isPDK = False
    isCreate = False
    TOP = None
    TEGRA_TOP = None
    P4ROOT = None
    BUILD_FLAVOR = None
    TARGET_BOARD = "generic"
    TARGET_PLATFORM = "generic"
    #The following are replaced during SDK package generation
    #with drive folder names for foundation, linux and qnx"
    #not used in dev env
    NV_SDK_NAME_FOUNDATION = "unified_flash"
    NV_SDK_NAME_LINUX = "drive-linux"
    NV_SDK_NAME_QNX = "drive-qnx"
    TEST_AUTOMATION = False
    NV_OUTDIR = None
    SAFETY_BUILD = None
    NV_QNX_BASE = None
    QNX_HOST = None
    QNX_TARGET = None
    NV_BUILD_WORK_LOAD = None
    NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT = None
    PDK_TOP = None
    BOOTBURN_PYTEST = False
    NV_BUILD_CONFIGURATION_IS_SAFETY = False
    NV_BOOTBURN_SAFETY_ALL_OPTIONS=False
    NV_BOOTBURN_SECURE_CHECK=True
    p_autogenPath = None
    safetyFileSDK = "version-nv-sdk-safety.txt"
    p_BurnDir = None
    p_BurnStorageCfgs = None
    p_TcumuxerSupport = None
    p_TcumuxerDir = None
    p_BCT = None
    s_BR_CID = None
    f_WrapperShellScrip = "wr_sh.sh"
    s_UpdatePartitions = []
    s_CreatePartialPartitions = []
    n_RBVerification = False
    n_EnableTraceCall = False
    n_EngSample = False
    n_DevBoardConnected = False
    s_BootDevice = "qspi"
    s_BoardName = ""
    n_FlashHypervisor = True
    f_OutFile = "log.txt"
    TempDir = None
    p_fsUtils = None
    skuArguments = {}
    p_PlatformInternalBCT = None
    p_PlatformCustomerBCT = None
    p_bctPath = None
    p_FsCreationToolPath = None
    p_FsAndroidToolPath = None
    n_FlashLinux = False
    n_Asymmetric = False
    s_debugOutput = False
    f_UfsProvisionCfg = None
    n_FlashQnx = False
    s_DtbImgSuffix = ""
    n_UseDebugBins = False
    n_GenBinsOnly = False
    n_SkipSkuValidate = False
    p_TempDumpPath = None
    p_TempOutDir = None
    s_ChipRevision = 0
    f_PkcKeyFilePath = None
    f_EncKeyFilePath = None
    f_PlEncKeyFilePath = None
    n_SecurebootFlash = False
    n_EncryptedbootFlash = False
    n_PlEncryptedbootFlash = False
    n_DRAMECCEnabled = False
    p_OutDirPath = None
    s_DtOdmdata = None
    p_FlashPath = None
    p_FlashingDtbPath = None
    p_BpmpFWFlashingDtb = None
    s_BadPage = "bad_page.bin"
    s_QnxDtbImgName = "qnx.dtb"
    s_LinuxDtbImgName = "linux.dtb"
    s_StorageDtbImgName = None
    s_BPMPStorageDtbImgName = "bpmp.dtb"
    f_FlashingCfg = "quickboot_flashing.cfg"
    f_PkcKeyFile = "pkc_priv.txt"
    f_EncKeyFile = "enc_symmetric.txt"
    f_PlEncKeyFile = "pl_enc_symmetric.txt"
    HSMStr = ""
    HSMAuthStr = ""
    b_CreateOptionalPartitions = False
    p_HyperVisorCfgsPath = None
    n_UFSProvisioningEnable = False
    f_MB2Applet = "applet_t264.bin"
    s_Mb2AppletMagicId = ""
    f_FlashingMB2Applet = "applet_t264.bin"
    f_NvMb2BinImage = "nvtboot.bin"
    unSafe_MB2 = False
    f_NvICTBinImage ="ist_ict.bin"
    f_NvISTBinImage="ist_bpmp"
    p_NvICTBinPath=None
    p_NvISTBinPath=None
    p_NvGrCsvPath = None
    n_UseDebugBins = False
    n_SecurebootFlash = False
    n_DRAMECCEnabled = False
    n_ChipVersionSet = False
    s_NvSkuArgs = None
    p_FuseBypass = None
    p_StorageDtbPath = None
    p_StorageGenericDtbPath = None
    p_LinuxStorageDtbPath = None
    p_LinuxStorageGenericDtbPath = None
    p_WB0Private = None
    p_FlashCfgPath = None
    f_FlashCfg = None
    f_CustomFlashCfg = None
    p_FlashingCfgPath = None
    s_NvSkuPreservArgs = ""
    p_Hardware = None
    p_Platform = None
    p_PlatformConfig = None
    p_PlatformCommonBCT = None
    p_PlatformSimBCT = None
    f_BrBctDevParam = None
    p_BpmpFwDtb = None
    p_TopOutDataPath = None
    p_Hwinc = None
    n_GrDump = False
    n_ChipId = 0
    n_ChipVersion = 0
    n_SpaceRequired = 100000000 # 100 MB for the binaries copied before calling nvimagegen
    n_WaitOnQueue = True
    d_ProcessInfo = {"child":False}

    s_VerStr = "a02-"
    s_HypConfig = None
    mediaCombination = {}
    n_KernelVariantRT = False
    n_EcidLength = 25
    s_EcidInImage = None
    n_SecDbgCtrl = None
    p_AppletDir = "applet"
    f_AppletBin = "applet_blob.bin"
    f_MB1SoftfuseBin = "sfuse.bin"
    configFileList = []
    f_StorageDtbNameAbs = None
    f_StorageDtbName = None
    f_BpmpDtbNameAbs = None
    f_BpmpDtbName = None
    f_MembctBin = ["membct_0.bct", "membct_1.bct", "membct_2.bct", "membct_3.bct", "membct_4.bct", "membct_5.bct", "membct_6.bct", "membct_7.bct"]
    n_EnableJtagBit = 1
    s_AdbSerialNum = ""
    s_AppletCmd = ""
    s_TargetDeviceInfo = {}
    boardDefaultPaths = {}
    n_UsbAutosuspend = None
    s_McuPort = None
    s_AurixLockFile = None
    s_device_name_list = ["810c5b0000.spi", "a80b8d0000.ufshci"]
    s_RamCode = 0
    n_SkuValue = 0 # For multiple sku support
    s_EmmcErasedArray = []
    s_Tegra_B = False
    s_Tegra_C = False
    p_fp_platcfg = None
    platformConfig = {}
    n_ChipID = 0

    f_BomDataTableFile = "bom_data_table.txt"
    n_minSysVerforTargetValidation = 3

    sysMonitor = None
    parsed_commandline = {}
    generateChain = None
    f_AutoLock = ""
    witheenv = True
    f_customerDataBlobFile = "customer_data.bin"
    f_signedcustomerDataBlobFile = "signed_customer_data.bin"
    p_Mb1Headers = None
    f_TegrablCarveoutFile = "tegrabl_carveout_id.h"
    p_DtsIncludesJsonPath = ""
    s_UFS_debugfs_node = "/ufshci@a80b8d0000"
    s_dceName = None
    s_dceDtbName = None
    f_SignedCustomerDataSchema = None
    f_SignedCustomerData = None
    f_SignedCustomerDataUpdated = "signed_customer_data_updated.json" # updated with Inforom data, a file created during flashing
    s_InforomData = {}
    s_InforomSkuVersion = ""
    s_InforomProdInfo = ""
    s_InforomObjType = ""
    s_InforomSYSObjVersion = ""
    n_InforomT23xSYSObjMinVer = "6"
    s_InforomT23xObjName = "OAT"
    s_Product_Part_Number = ""
    n_ufshci = False
    s_ErasePartitionsToBrick = ["A_mb1-bootloader", "B_mb1-bootloader"]
    b_DisableUARTinMB1nMB2Bct = False
    devicetype = None
    f_baseBoardSkuMapping = "baseboard_sku_mapping.json"
    f_chipSkuMapping = "chipsku_mapping.json"
    b_boardFusedForSigning = False
    s_boardFusedForSigning = ""
    b_boardFusedForEncryption = False
    s_boardFusedForEncryption = ""
    b_UserSignsImages = False
    s_DeviceType = ""
    s_FuseStatus = ""
    b_findBoardName = False
    s_baseBoardName = ""
    s_defaultChipSku = ""
    s_SocSkuBoardModifier = ""
    s_SocSkuName = ""
    s_BoardName = ""
    b_detectBoardDone = False
    s_findBoardDataDict = {'DeviceType' : 'UNKNOWN', 'FuseStatus' : 'UNKNOWN', 'BaseBoardName' : 'UNKNOWN', 'SocSkuName' : 'UNKNOWN', 'BoardName' : 'UNKNOWN', "FusedForSigning" : "UNKNOWN",  "FusedForEncryption" : "UNKNOWN"}
    f_findBoardName = "board_name.txt"
    s_FindBoardStr = ""
    b_findBoardinNativeQnx = False
    b_findBoardinNativeLinux = False
    f_FirmwareJson = "firmware.json"
    #end of find_board defines

    mergeChains = None
    fskpBinName = "blob_fskp_updated_aligned_sigheader_encrypt.signed" # Hardcoded fskp blob (firmware + fuse blob) name expected at the path specificed in PCT storage config
    fskpBctPath = None
    brBctLayoutJsonFile = "bct_blob_layout.json"
    p_logs = None
    f_BoardConfigFile = ""
    p_SocBoardConfigsPath = ""
    p_BoardConfigsPath = ""
    headers = None
    b_boardConnected = False
    f_skuInfoJson = "skuInfo.json"
    f_bpmpInfoJson = "bpmpInfo.json"
    s_SkuInfo = None
    fskpBrBct = "br_bct_BR_sigheader.bct"
    s_buildVersion = ""
    l_verFileNames = ["version-nv-sdk-safety.txt", "version-nv-pdk-safety.txt", "version-nv-sdk.txt", "version-nv-pdk.txt"]
    p_PyFlashPath = None
    f_refPvitFile = []
    l_selectedChains = []
    d_PvitChainNPartitions = { 'A': {"selState":"NotSelected", "pvitState":"PvitDisabled", "pvitPart":"A_pvit", "pvitTgtUsrFile":"", "pvitImg":"A_PVIT_Image.bin", "partList":[]}, 'B': {"selState":"NotSelected", "pvitState":"PvitDisabled", "pvitPart":"B_pvit", "pvitTgtUsrFile":"", "pvitImg":"B_PVIT_Image.bin", "partList":[]}, 'C' : {"selState":"NotSelected", "pvitState":"PvitDisabled", "pvitPart":"C_pvit", "pvitTgtUsrFile":"", "pvitImg":"C_PVIT_Image.bin", "partList":[]} }
    s_PVITImgMagicId = "PVIT"
    skipBctPreserve = False
    # tegrabct_v2 --parse-error-mode (values are enforced by tegrabct_v2)
    tegrabct_parse_error_mode = 'quiet-continue'
    f_pcpKeyListFile = None # This is keylist.xml for bct's pcp, pcp digests
    is_l4t = False
    dieId="die0" # Default die id
    is_sim = False
    isminiPDK = False
    mb2AppletBarrier = None
    BpmpFwDtbList = None
    KernelDtbList = None
    b_IsMultiSkuEnabled = False
    p_PyFlashPath = None
    b_IsBrBctSignedCustomerDataPreserved = False
    p_ClassKeyDir = None
    n_FlashFirmware = False
    s_BrickOnCoreFWFlashFail = False
    s_BrickOnFirmwareUpgradeFail = False
    p_boardFirmware = ""
    p_McuFwBin = ""
    p_McuUpdtScript = ""
    f_McuUpdtScriptAbs = ""
    f_McuFwBinAbs = ""
    f_FirmwareJson = "firmware.json"
    n_NoDuPvit = False
    s_L4TBootChainSelect = "A"

    def __init__(self, isPDK=False, isOfflineFlash=False):
        os.environ["LC_ALL"] = "C"
        #pytest not clearing some of these variables
        self.s_TargetDeviceInfo = {}
        self.skuArguments = {}
        self.f_customerDataSchemaFile = None
        self.skuArguments["s_NvSkuArgs"] = ""
        self.f_customerDataValueFile = None
        self.boardDefaultPaths = {}
        self.s_UpdatePartitions = []
        self.platformConfig = {}
        self.customerData = CustomerData({}, "customer-data-unsigned")
        self.customerDataProcessor = CustomerDataProcessor()
        self.signedCustomerData = CustomerData({}, "customer-data-signed")
        self.signedcustomerDataProcessor = CustomerDataProcessor()
        self.p_logs = os.getcwd()
        self.isminiPDK = os.path.isfile(os.path.join(os.getcwd(), "..", "board_configs", "miniPDK"))

        try:
            self.isPDK = isPDK
            NV_BUILD_CONFIGURATION_IS_DEBUG = self.getEnviornmetalVariable("NV_BUILD_CONFIGURATION_IS_DEBUG")
            NV_BUILD_CONFIGURATION_IS_DEVELOP = self.getEnviornmetalVariable("NV_BUILD_CONFIGURATION_IS_DEVELOP")

            enableAllOptionsSafety = self.getEnviornmetalVariable("NV_BOOTBURN_SAFETY_ALL_OPTIONS")
            if(enableAllOptionsSafety == "1"):
                self.NV_BOOTBURN_SAFETY_ALL_OPTIONS = True

            secureCheck = self.getEnviornmetalVariable("NV_BOOTBURN_SECURE_CHECK")
            if(secureCheck == "0"):
                self.NV_BOOTBURN_SECURE_CHECK = False

            self.NV_BUILD_KERNEL_VARIANT_PATH_UNIQUEFIER = self.getEnviornmetalVariable("NV_BUILD_KERNEL_VARIANT_PATH_UNIQUEFIER")
            if (self.NV_BUILD_KERNEL_VARIANT_PATH_UNIQUEFIER == None):
                self.NV_BUILD_KERNEL_VARIANT_PATH_UNIQUEFIER = "kernel-gvs_kernel_variant"
            self.PDK_TOP = self.getEnviornmetalVariable("PDK_TOP")
            self.TOP = self.getEnviornmetalVariable("TOP")
            self.TEGRA_TOP = self.getEnviornmetalVariable("TEGRA_TOP")
            self.P4ROOT = self.getEnviornmetalVariable("P4ROOT")
            self.BUILD_FLAVOR = self.getEnviornmetalVariable("BUILD_FLAVOR")
            self.BUILD_TYPE = self.getEnviornmetalVariable("BUILD_TYPE")
            self.NV_OUTDIR = self.getEnviornmetalVariable("NV_OUTDIR")
            if(self.NV_OUTDIR == None):
                self.witheenv = False
            self.NV_BUILD_CONFIGURATION_IS_SAFETY = self.getEnviornmetalVariable("NV_BUILD_CONFIGURATION_IS_SAFETY")
            target_platform = self.getEnviornmetalVariable("EMBEDDED_TARGET_PLATFORM")
            if(self.NV_BUILD_CONFIGURATION_IS_SAFETY == "1" or (target_platform != None and "safety" in target_platform)):
                self.NV_BUILD_CONFIGURATION_IS_SAFETY = True
                self.SAFETY_BUILD = True
            else:
                self.NV_BUILD_CONFIGURATION_IS_SAFETY = False
                self.SAFETY_BUILD = False

            self.TARGET_BOARD = self.getEnviornmetalVariable("NV_TARGET_BOARD")
            if(self.TARGET_BOARD == None):
                self.TARGET_BOARD = "generic"
            self.TEST_AUTOMATION = self.getEnviornmetalVariable("TEST_AUTOMATION")
            if(self.TEST_AUTOMATION == "1"):
                self.TEST_AUTOMATION = True
            else:
                self.TEST_AUTOMATION = False

            self.NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT = self.getEnviornmetalVariable("NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT")
            if(self.NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT == None):
                self.NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT = "none"

            self.NV_BUILD_WORK_LOAD = self.getEnviornmetalVariable("NV_BUILD_WORK_LOAD")
            self.NV_QNX_BASE = self.getEnviornmetalVariable("NV_QNX_BASE")
            if(self.NV_QNX_BASE == None):
                self.NV_QNX_BASE = self.getEnviornmetalVariable("QNX_BASE")

            if(self.BUILD_FLAVOR == None):
                if (NV_BUILD_CONFIGURATION_IS_DEBUG != None):
                    if(NV_BUILD_CONFIGURATION_IS_DEBUG == "0"):
                        if (NV_BUILD_CONFIGURATION_IS_DEVELOP != None and NV_BUILD_CONFIGURATION_IS_DEVELOP == "1"):
                            self.BUILD_FLAVOR="develop"
                        else:
                            self.BUILD_FLAVOR="release"
                    else:
                        self.BUILD_FLAVOR="debug"
                else:
                    if(self.TEST_AUTOMATION):
                        self.BUILD_FLAVOR = "debug"
                    else:
                        self.BUILD_FLAVOR = "release"

            if(self.BUILD_TYPE == None):
                self.BUILD_TYPE = "external"

            if(isPDK == False):
                # if enviornment is not set it will try and guess at it
                if(self.TEGRA_TOP == None):
                    top  = os.path.abspath(os.path.join("..","..","..",".."))
                    if(os.path.isdir(os.path.join(top, "flash-tools", "legacy"))):
                        #need absolute path not relative
                        self.TEGRA_TOP = os.path.abspath(top)
                        self.TOP = self.TEGRA_TOP
                    else:
                        msg = "TEGRA_TOP not set and can't determine from PWD"
                        AbnormalTermination(msg, nverror.NvError_BadParameter)

                if(self.witheenv == False and self.TEST_AUTOMATION == False):
                    outDir = self.getOutDir("foundation", self.TARGET_BOARD, self.BUILD_FLAVOR, self.NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT)
                    self.NV_OUTDIR = os.path.join(self.TEGRA_TOP, "out", outDir[0])
                    if(os.path.isdir(self.NV_OUTDIR) == False):
                        msg = "Out dir does not exist - " + self.NV_OUTDIR + "\n"
                        msg += "Has choose been called and the foundation been built?"
                        AbnormalTermination(msg, nverror.NvError_DirOperationFailed)
            else:
                if(self.PDK_TOP == None):
                    #Assume they are in the bootburn directory and try and find it
                    pdktop_found=False
                    pdktop  = os.path.abspath(os.path.join("..","..", ".."))
                    if(os.path.isdir(os.path.join(pdktop, "..", self.NV_SDK_NAME_FOUNDATION)) == True):
                        pdktop_found=True
                        pdktop = os.path.abspath(os.path.join(pdktop, ".."))
                    elif(os.path.isdir(os.path.join(pdktop, "platforms")) == True):
                        pdktop_found=True
                        self.isCreate = True
                    elif(isOfflineFlash):
                        pdktop_found = True
                        self.isCreate = True
                    else:
                        for root, dirs, files in os.walk(pdktop, topdown=True, followlinks=True):
                            for name in files:
                                if name == "TargetInfo.txt":
                                    pdktop_found = True
                                    self.isCreate = True
                                    break
                            if pdktop_found:
                                break
                    if(pdktop_found == False):
                        msg = "Can't determine PDK top folder from PWD"
                        AbnormalTermination(msg, nverror.NvError_BadParameter)
                    self.PDK_TOP = os.path.abspath(pdktop)

        except Exception as e:
            msg = "TargetConfig failed to initialize"
            print(msg + " " + str(e))
            traceback.print_exc()
            AbnormalTermination(msg, nverror.NvError_BadParameter)

    def getEnviornmetalVariable(self, var):
        environVar = None
        try:
            environVar = os.environ[var]
        except:
            if(self.s_debugOutput):
                print("Environmental variable " + var + " not set")

        return environVar

    def getBCTDerConstFilePath(self):
        return os.path.join(self.p_TopOutDataPath, 'der_const_val.bin')

    def getPcpFilePath(self):
        return os.path.join(self.p_TopOutDataPath, 'pcp_file.bin')

    def getPcpsHashFilePath(self):
        return os.path.join(self.p_TopOutDataPath, 'pcps_hash_file.bin')

    def copyPkcKey(self, dest_path, rename=True):
        # if rename == true, copy f_PkcKeyFilePath to dest_path/f_PkcKeyFile
        # else copy f_PkcKeyFilePath to dest_path/basename(f_PkcKeyFilePath)
        # We default to attempt to copy xmss cache and pub key by assuming fixed
        # format file names b/c we don't know the key type yet but we may need them

        if rename == True:
            dest_basename = self.f_PkcKeyFile
        else:
            dest_basename = os.path.basename(self.f_PkcKeyFilePath)
        full_path = os.path.join(dest_path, dest_basename)
        if os.path.exists(full_path) == False:
            import shutil
            shutil.copyfile(self.f_PkcKeyFilePath, full_path)
            src_ext = [os.path.splitext(self.f_PkcKeyFilePath)[1] + ".cache", ".pub"]
            dest_ext = [os.path.splitext(dest_basename)[1] + ".cache", ".pub"]
            for i in range(0, len(src_ext)):
                xmss_src_path = os.path.splitext(self.f_PkcKeyFilePath)[0] + src_ext[i]
                if os.path.exists(xmss_src_path) == True:
                    xmss_basename = os.path.splitext(dest_basename)[0] + dest_ext[i]
                    xmss_dest_path = os.path.join(dest_path, xmss_basename)
                    print("Copying {} to {}".format(xmss_src_path, xmss_dest_path))
                    shutil.copyfile(xmss_src_path, xmss_dest_path)
        return full_path

    def getPkcKeyFilePathFromOutData(self):
        # copy the file to out if not present and return the path
        return self.copyPkcKey(self.p_TopOutDataPath, rename=False)

    def getPcpKeyListFile(self):
        if self.f_pcpKeyListFile == None:
            return None
        return os.path.join(self.p_TopOutDataPath, self.f_pcpKeyListFile)

    def setPcpKeyListFile(self, keyListFile):
        self.f_pcpKeyListFile = keyListFile

    def SetDefaultPaths(self):
        if(self.isPDK):
            self.SetBootBurnPaths()
        else:
            self.SetBootBurnInternalSetupPath()
    def GetChipFamily(self):
        #default to t264
        chipFamily = "t264"
        if("s_ChipFamily" in self.boardDefaultPaths):
            chipFamily = self.boardDefaultPaths["s_ChipFamily"]

        return chipFamily

    def GetSocType(self):
        #default to t234
        #fixme need to make this more generic
        soc = "t264"
        if("s_Soc" in self.boardDefaultPaths):
            soc = self.boardDefaultPaths["s_Soc"]

        return soc

    def IsNvKeyFilesNeeded(self):
        is_needed = False
        if("s_Soc" in self.boardDefaultPaths):
            soc = self.boardDefaultPaths["s_Soc"]
            is_needed = soc in ['t268']
        return is_needed

    def JsonInitRamcode(self):
        ramCode = 0
        if("s_RamCode" in self.boardDefaultPaths):
            ramCode = int(self.boardDefaultPaths["s_RamCode"])

        if ((ramCode >= 0) and (ramCode <= 15)):
            self.s_RamCode = ramCode
        else:
            AbnormalTermination("Ramcode must be between 0 and 15", nverror.NvError_InvalidArgument)

    def GetChipID(self):
        #default to xavier 0x19
        ID = "0x26"
        if("s_chipID" in self.boardDefaultPaths):
            ID = self.boardDefaultPaths["s_chipID"]
        return ID

    def getChipIDMinor(self, chipID):
        minor = ""
        if("s_chipIDMinor" in self.boardDefaultPaths):
            minor = self.boardDefaultPaths["s_chipIDMinor"]
        return minor

    def GetBuildVersion(self, fileName):
        #get the value saved in the file name passed
        with open(fileName, "r") as srcverfd:
            flines = list(srcverfd)
            if (len(flines) > 1):
                print("source version file  is having more than one line");
            self.s_buildVersion = (flines[0].strip()).split()[0]

    def getFileList(self, chipFamily):
        fileListPath = os.path.dirname(os.path.realpath(__file__))
        if(self.isPDK) and (self.BOOTBURN_PYTEST == False):
            fileListPath = os.path.abspath(os.path.join(fileListPath, "..", "board_configs", chipFamily))
        else:
            fileListPath = os.path.abspath(os.path.join(fileListPath, "..", "..", "board_configs", chipFamily))

        fileListPath = os.path.join(fileListPath, "filelist_" + chipFamily + ".json")
        try:
            fid = open(fileListPath, "r")
            data = fid.read()
            fid.close()
            jsonData = json.loads(data)
            self.s_device_name_list = jsonData["s_device_name_list"]
            self.filelist = jsonData["filenames"]
        except Exception as e:
            print(str(e) + "\n")
            traceback.print_exc()
            print("\nInvalid file list for chip family " + str(chipFamily) + " -- " + str(fileListPath))
            AbnormalTermination("Invalid file list for " + str(chipFamily), nverror.NvError_NotInitialized)

    def GetPDKBuildVersion(self, foundation):
        buildFileName = ""
        for verFile in self.l_verFileNames:
            if (True == os.path.exists(os.path.join(foundation, verFile))):
                buildFileName = os.path.join(foundation, verFile)
                break

        if ("flash_bsp" not in self.sysMonitor.identifier):
            if (len(buildFileName) > 0) and (True == os.path.exists(buildFileName)):
                self.GetBuildVersion(buildFileName)
            elif (self.TEST_AUTOMATION == True):
                print ("**** Build Version File Not Found in GVS run **** \n")
            else:
                AbnormalTermination("Build version File doesn't exist", nverror.NvError_FileNotFound)

    def GetInternalBuildVersion(self):
        fileName = os.path.join(self.TEGRA_TOP, ".repo", "manifests.git", "FETCH_HEAD")
        if ("flash_bsp" not in self.sysMonitor.identifier):
            if (True == os.path.exists(fileName)):
                self.GetBuildVersion(fileName)
            elif (self.TEST_AUTOMATION == True):
                print("**** Build Version File Not Found in GVS run **** \n")
            else:
                AbnormalTermination("Build version File is not found", nverror.NvError_FileNotFound)

    def SetHpseSbPaths(self):
        chipFamily = self.GetChipFamily()
        if (self.isPDK != True):
            if (self.n_EngSample == True):
                self.p_HpseKrnlBin = os.path.join(self.NV_OUTDIR, "nvidia", "spvault", chipFamily, "integration", "bind")
                self.p_HpseCpioBin = os.path.join(self.NV_OUTDIR, "nvidia", "spvault", chipFamily, "integration", "bind")
                self.p_HpseBlBin = os.path.join(self.TEGRA_TOP, "psc_bl1", "private-t264")
            else:
                self.p_HpseKrnlBin = os.path.join(self.TEGRA_TOP, "oespfw", "prebuilt-public-t264")
                self.p_HpseCpioBin = os.path.join(self.TEGRA_TOP, "oespfw", "prebuilt-public-t264")
                self.p_HpseBlBin = os.path.join(self.TEGRA_TOP, "psc_bl1", "public-t264")
        else:
            if (self.n_EngSample == True):
                self.p_HpseBlBin   = os.path.join(self.PDK_TOP, "foundation", "psc_bl1", f"private-{chipFamily}")
                self.p_HpseKrnlBin = os.path.join(self.PDK_TOP, "foundation", "spvault", chipFamily, "integration", "bind")
                self.p_HpseCpioBin = os.path.join(self.PDK_TOP, "foundation", "spvault", chipFamily, "integration", "bind")
            else:
                self.p_HpseBlBin = os.path.join(self.PDK_TOP, self.NV_SDK_NAME_FOUNDATION, "firmware", "bin", chipFamily, "psc_bl1")
                self.p_HpseKrnlBin = os.path.join(self.PDK_TOP, self.NV_SDK_NAME_FOUNDATION, "firmware", "bin", chipFamily, "oespfw")
                self.p_HpseCpioBin = os.path.join(self.PDK_TOP, self.NV_SDK_NAME_FOUNDATION, "firmware", "bin", chipFamily, "oespfw")

    def SetBootBurnInternalSetupPath(self):

        if (not self.TEGRA_TOP):
            print("Must set environmental variable TEGRA_TOP when working on internal TOT")
            AbnormalTermination("Environment not set", nverror.NvError_NotInitialized)

        chipFamily = self.GetChipFamily()
        socType = self.GetSocType()
        self.getFileList(chipFamily)
        self.GetInternalBuildVersion()

        p_QbDev = ""
        if (self.n_UseDebugBins):
            p_QbDev = "_dev"

        p_QbSafe = ""
        if (self.SAFETY_BUILD):
            p_QbSafe = "_safe"

        bootloaderOutDir = os.path.join(self.NV_OUTDIR, "nvidia", "bootloader")
        if (self.TEST_AUTOMATION == True):
            self.p_FsCreationToolPath = os.path.abspath(os.path.join(self.TEGRA_TOP, "..", "sw", "embedded", "external-prebuilt", "tools"))
            self.p_FsAndroidToolPath = os.path.join(self.TEGRA_TOP, "..", "sw", "embedded", "external-prebuilt", "simg2img", "android11")
            self.p_FsCreationmkfsPath = os.path.abspath(os.path.join(self.TEGRA_TOP, "..", "sw", "embedded", "external-prebuilt", "tools", "linux", "mkfs", "util-linux-2.31.1"))
            self.p_Mb1Headers = os.path.join(bootloaderOutDir, "partner", chipFamily, "mb1-headers")
        else:
            if (not self.P4ROOT):
                print("Must set environmental variable P4ROOT when working on internal TOT")
                AbnormalTermination("Environment not set", nverror.NvError_NotInitialized)

            self.p_FsCreationToolPath = os.path.join(self.P4ROOT, "sw", "embedded", "external-prebuilt", "tools" )
            self.p_FsAndroidToolPath = os.path.join(self.P4ROOT, "sw", "embedded", "external-prebuilt", "simg2img", "android11")
            self.p_FsCreationmkfsPath = os.path.join(self.P4ROOT, "sw", "embedded", "external-prebuilt", "tools", "linux", "mkfs", "util-linux-2.31.1")
            self.p_Mb1Headers = os.path.join(self.TEGRA_TOP, "bootloader", "partner", chipFamily, "mb1-headers")

        bootburnPath = os.path.join(self.TEGRA_TOP, "flash-tools", "legacy")
        tegraflash = os.path.join(self.TEGRA_TOP, "flash-tools", "legacy")
        tegraboardfirmware = os.path.join(self.TEGRA_TOP, "flash-tools", "legacy", "firmware")
        flashToolsOutDir = os.path.join(self.NV_OUTDIR, "nvidia", "flash-tools", "legacy")
        tegrasignPriv = os.path.join(self.TEGRA_TOP, "security-private", "keys")
        tegrasignDevKeysPriv = os.path.join(self.TEGRA_TOP, "bootloader", "mb1-private", "soc", "t239", "scripts", "mb1_sign")
        self.p_FlashPath = os.path.join(self.NV_OUTDIR, "nvidia", "qb_flashtools")
        self.p_NvGrCsvPath = os.path.join(bootburnPath, "boards", "t234ref", "grinfo")
        self.p_Hardware = os.path.join(self.TEGRA_TOP, "hardware", "nvidia")
        self.p_Platform = os.path.join(self.p_Hardware, "platform", chipFamily)
        self.p_PlatformConfig = os.path.join(self.p_Platform, "automotive", "automotive-platform-configs")
        self.p_boardFirmware = os.path.join(tegraboardfirmware, self.f_FirmwareJson)

        self.f_MB2AppletPath = os.path.join(bootloaderOutDir, "applet-private", "soc", "t264", "build", "auto", "standard-boot")
        self.s_Mb2AppletMagicId = "MB2A"
        self.f_FlashingMB2AppletPath = self.f_MB2AppletPath
        if (self.f_SignedCustomerDataSchema == None):
            self.f_SignedCustomerDataSchema = os.path.join(bootburnPath, "scripts", "t264_bootburn_py", "nv-customer-data-schema.json")
        self.p_FlashingNvQbDtbPath = os.path.abspath(os.path.join(bootburnPath, "flashing_kernel", "flashing_bl", chipFamily))
        self.p_BurnStorageCfgs = os.path.join(bootburnPath, "storage_configs", chipFamily)
        self.p_TcumuxerSupport = os.path.join(self.TEGRA_TOP, "tools", "tcu_muxer")
        self.p_TcumuxerDir = os.path.abspath(os.path.join(self.NV_OUTDIR, "nvidia", "tools", "tcu_muxer", "tmake-hostcc"))

        if(self.is_l4t or self.is_sim):
            self.p_SocBoardConfigsPath = os.path.join(bootburnPath, "board_configs")
        else:
            self.p_SocBoardConfigsPath = self.p_PlatformConfig

        self.p_BoardConfigsPath = self.p_SocBoardConfigsPath
        self.p_FirmwareBin = os.path.join(self.TEGRA_TOP, "3rdparty")
        self.p_WB0Partner = os.path.join(self.TEGRA_TOP, "warmboot-firmware", "partner-" + chipFamily)
        self.p_fsUtils = os.path.join(self.TEGRA_TOP, "embedded", "tools", "utils")

        self.f_baseBoardSkuMapping = os.path.join(bootburnPath, "board_configs", "t264", self.f_baseBoardSkuMapping)
        self.f_chipSkuMapping = os.path.join(bootburnPath, "board_configs", "t264",self.f_chipSkuMapping)
        self.p_DtsIncludesJsonPath = os.path.join(bootburnPath, "board_configs", "t264")
        self.f_skuInfoJson = os.path.join(bootburnPath, "board_configs", "t264", self.f_skuInfoJson)
        self.f_bpmpInfoJson = os.path.join(bootburnPath, "board_configs", "t264", self.f_bpmpInfoJson)

        self.flashUtils.f_AdbTool = os.path.join(self.p_fsUtils, self.flashUtils.f_AdbTool)
        self.flashUtils.f_TegraBct = os.path.join(flashToolsOutDir, "tegrabct_v2-hostcc", self.flashUtils.f_TegraBct)
        self.flashUtils.f_TegraHost = os.path.join(flashToolsOutDir, "tegrahost_v2-hostcc",self.flashUtils.f_TegraHost)
        self.flashUtils.f_TegraParser = os.path.join(flashToolsOutDir, "tegraparser_v2-hostcc", self.flashUtils.f_TegraParser)

        self.p_HpsePctBin = os.path.join(self.NV_OUTDIR, "nvidia", "spvault", "conf-blob", "t26x")
        self.p_HpseFindBoardPctBin = os.path.join(self.TEGRA_TOP, "oespfw", "prebuilt-public-t264", "l4t")
        for i in range(len(self.flashUtils.f_TegraSign)):
            if self.flashUtils.f_TegraSign[i] == "tegraopenssl":
                self.flashUtils.f_TegraSign[i] = os.path.join(flashToolsOutDir, "tegraopenssl-hostcc", self.flashUtils.f_TegraSign[i])
            elif self.flashUtils.f_TegraSign[i] == "xmss-sign":
                self.flashUtils.f_TegraSign[i] = os.path.join(flashToolsOutDir, "xmss-sign-hostcc", self.flashUtils.f_TegraSign[i])
            else:
                self.flashUtils.f_TegraSign[i] = os.path.join(tegraflash, "tegrasign_v3", self.flashUtils.f_TegraSign[i])

        for i in range(len(self.flashUtils.f_TegraSignPriv)):
            self.flashUtils.f_TegraSignPriv[i] = os.path.join(tegrasignPriv, self.flashUtils.f_TegraSignPriv[i])

        for i in range(len(self.flashUtils.f_TegraSignDevKeyFiles)):
            self.flashUtils.f_TegraSignDevKeyFiles[i] = os.path.join(tegrasignDevKeysPriv, self.flashUtils.f_TegraSignDevKeyFiles[i])

        tegraSignDebugYaml = os.path.join(tegraflash, "tegrasign_v3", "tegrasign_v3_debug.yaml")
        if os.path.exists(tegraSignDebugYaml):
            self.flashUtils.f_TegraSign.append(tegraSignDebugYaml)

        self.p_PyFlashPath = os.path.join(self.TEGRA_TOP, "embedded", "tools", "scripts", "bootburn_py")
        self.flashUtils.f_TegraRcm = os.path.join(flashToolsOutDir, "tegrarcm_v2-hostcc", self.flashUtils.f_TegraRcm)
        self.flashUtils.f_CompressLz4 = os.path.join(self.p_FlashPath, self.flashUtils.f_CompressLz4)
        self.flashUtils.f_NvSkuInfo = os.path.join(self.p_FlashPath, self.flashUtils.f_NvSkuInfo)
        self.flashUtils.f_NvBchValidate = os.path.join(self.p_FlashPath, self.flashUtils.f_NvBchValidate)
        self.flashUtils.f_NvImageGen = os.path.join(self.p_FlashPath, self.flashUtils.f_NvImageGen)
        self.flashUtils.f_NvImageSign = os.path.join(self.p_FlashPath, self.flashUtils.f_NvImageSign)
        self.flashUtils.f_NvDDTool = os.path.join(self.p_FlashPath, self.flashUtils.f_NvDDTool)
        self.flashUtils.f_NvOptinFuse = os.path.join(self.NV_OUTDIR, "nvidia", "core-private", "utils", "optin_fuse-embedded-linux_64", self.flashUtils.f_NvOptinFuse)
        self.flashUtils.f_NvSimgDump = os.path.join(self.p_FlashPath, self.flashUtils.f_NvSimgDump)
        self.flashUtils.f_Nvpt = os.path.join(self.p_FlashPath, self.flashUtils.f_Nvpt)
        self.flashUtils.f_NvResign = os.path.join(self.p_FlashPath, self.flashUtils.f_NvResign)
        self.p_Hwinc = os.path.join(self.TEGRA_TOP, "hwinc-" + chipFamily)

        self.f_WrapperShellScrip = os.path.join(self.p_fsUtils, self.f_WrapperShellScrip)

        self.p_FlashCfgPath = os.path.abspath(os.path.join(bootburnPath, "storage_configs", self.GetChipFamily()))
        self.p_FlashingCfgPath = os.path.abspath(os.path.join(bootburnPath, "flashing_kernel", "flashing_configs", chipFamily))
        self.p_BpmpFwDtb = os.path.join(self.NV_OUTDIR, "nvidia", "bpmp-dtb")

        if (self.TEST_AUTOMATION == True):
            self.s_NvSkuPreservArgs = self.s_NvSkuPreservArgs + " --skuverify"

        linuxOutDir = self.getOutDir("linux", self.TARGET_BOARD, self.BUILD_FLAVOR, self.NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT)
        linuxPath = os.path.join(linuxOutDir[0], "nvidia", self.NV_BUILD_KERNEL_VARIANT_PATH_UNIQUEFIER, "kernel-rt_patches-nvidia-oot", "nvidia-oot", "device-tree", "platform", "legacy-dts", "dtbs")
        linuxGenericPath = os.path.join(linuxOutDir[0], "nvidia", self.NV_BUILD_KERNEL_VARIANT_PATH_UNIQUEFIER, "kernel-rt_patches-nvidia-oot", "nvidia-oot", "device-tree", "platform", "generic-dts", "dtbs")
        linuxSysPath = os.path.join(linuxOutDir[0], "systemimage", self.NV_BUILD_KERNEL_VARIANT_PATH_UNIQUEFIER, "boot", "dtbs-rt", "legacy-dtbs")
        linuxSysGenericPath = os.path.join(linuxOutDir[0], "systemimage", self.NV_BUILD_KERNEL_VARIANT_PATH_UNIQUEFIER, "boot", "dtbs-rt", "generic-dtbs")
        # Prefer the path from systemimage
        if (os.path.isdir(os.path.join(self.TEGRA_TOP, linuxSysPath))):
            self.p_LinuxStorageDtbPath = os.path.abspath(os.path.join(self.TEGRA_TOP, linuxSysPath))
            self.p_LinuxStorageGenericDtbPath = os.path.abspath(os.path.join(self.TEGRA_TOP, linuxSysGenericPath))
        else:
            self.p_LinuxStorageDtbPath = os.path.abspath(os.path.join(self.TEGRA_TOP, linuxPath))
            self.p_LinuxStorageGenericDtbPath = os.path.abspath(os.path.join(self.TEGRA_TOP, linuxGenericPath))

        # WAR : This will be reverted after dtb is generated at usual path
        nvidiapath = os.path.join(self.p_LinuxStorageDtbPath, "nvidia")
        if (os.path.isdir(nvidiapath)):
            self.p_LinuxStorageDtbPath = os.path.join(self.p_LinuxStorageDtbPath, "nvidia")

        nvidiagenericpath = os.path.join(self.p_LinuxStorageGenericDtbPath, "nvidia")
        if (os.path.isdir(nvidiagenericpath)):
            self.p_LinuxStorageGenericDtbPath = os.path.join(self.p_LinuxStorageGenericDtbPath, "nvidia")

        if (self.n_FlashQnx):
            if (self.TEST_AUTOMATION == True):
                self.p_StorageDtbPath = os.path.abspath(os.path.join(self.TEGRA_TOP, "..", "nvidia", "data-driven"))
            else:
                qnxOutDir = self.getOutDir("qnx", self.TARGET_BOARD, self.BUILD_FLAVOR, self.NV_BUILD_CONFIGURATION_EMBEDDED_VARIANT)
                qnxPath = os.path.join(qnxOutDir[0], "nvidia", "data-driven")
                if (os.path.isdir(os.path.join(self.TEGRA_TOP, "..", "qnx", qnxPath))):
                    self.p_StorageDtbPath = os.path.abspath(os.path.join(self.TEGRA_TOP, "..", "qnx", qnxPath))
                else:
                    self.p_StorageDtbPath = os.path.join(self.TEGRA_TOP, qnxPath)
        else:
            if (self.TEST_AUTOMATION == True):
                self.p_LinuxStorageDtbPath = self.p_StorageDtbPath = os.path.abspath(os.path.join(self.TEGRA_TOP, "..", "_out", self.BUILD_FLAVOR + "_generic-linux", "_armv6", "boot", "dtbs", "legacy-dtbs"))
                self.p_LinuxStorageGenericDtbPath = self.p_StorageGenericDtbPath = os.path.abspath(os.path.join(self.TEGRA_TOP, "..", "_out", self.BUILD_FLAVOR + "_generic-linux", "_armv6", "boot", "dtbs", "generic-dtbs"))
            else:
                self.p_StorageDtbPath = self.p_LinuxStorageDtbPath
                self.p_StorageGenericDtbPath = self.p_LinuxStorageGenericDtbPath

        # WAR : This will be reverted after dtb is generated at usual path
        nvidiapath = os.path.join(self.p_StorageDtbPath, "nvidia")
        if (os.path.isdir(nvidiapath)):
           self.p_StorageDtbPath = os.path.join(self.p_StorageDtbPath, "nvidia")

        self.p_HyperVisorCfgsPath = os.path.abspath(os.path.join(self.NV_OUTDIR, "nvidia", "hypervisor", self.s_BoardName))
        self.p_ClassKeyDir = os.path.abspath(self.NV_OUTDIR)

        if(self.p_fp_platcfg == None):
            self.p_fp_platcfg = os.path.join(self.p_HyperVisorCfgsPath, "platform_config.json")

        self.p_FlashingDtbPath = os.path.abspath(os.path.join(bootburnPath, "flashing_kernel", "kernel", chipFamily))
        self.p_BpmpFWFlashingDtb = os.path.join(bootburnPath, "flashing_kernel", "flashing_fw", chipFamily)

        self.p_BCT = self.p_Platform
        # DriveOS internal config
        self.p_PlatformInternalBCT = os.path.join(self.p_Platform, "automotive", "automotive-platform-configs", "driveos_internal_config", "bct")
        # DriveOS customer config
        # We do not specify the path to a sub-directory within the driveos_customer_config directory because as customer can create their own platforms under this folder
        # and we don't want to hardcode the path for the customer bct
        self.p_PlatformCustomerBCT = os.path.join(self.p_Platform, "automotive", "automotive-platform-configs", "driveos_customer_config")
        self.p_PlatformCommonBCT = os.path.join(self.p_Platform, "common", "bct")
        self.p_PlatformSimBCT = os.path.join(self.p_Platform, "sim", "bct")
        self.p_PlatformSOC = os.path.join(self.p_Hardware, "soc")

        if (self.TEST_AUTOMATION == True):
            self.p_PlatformMB1Headers = os.path.join(bootloaderOutDir, "abi-partner", "soc", chipFamily, "mb1")
        else:
            self.p_PlatformMB1Headers = os.path.join(self.TEGRA_TOP, "bootloader", "abi-partner", "soc", chipFamily, "mb1")
        self.p_FuseBypassPrivate = os.path.join(bootburnPath, "flashing_kernel", "flashing_fw", chipFamily)

        # Set the QNX toolchain variables for internal usage. Setting these variables
        # irrespective of the OS flashed because there is no way to identify if QNX is
        # flashed with the Hypervisor and there is no side-effect on flashing other OS.
        if(self.NV_QNX_BASE != None):
            os.environ["QNX_BASE"] = str(self.NV_QNX_BASE)
            self.QNX_HOST = os.path.join(self.NV_QNX_BASE, "host", "linux", "x86_64")
            os.environ["QNX_HOST"] = str(self.QNX_HOST)
            self.QNX_TARGET = os.path.join(self.NV_QNX_BASE, "target", "qnx7")
            os.environ["QNX_TARGET"] = str(self.QNX_TARGET)

        self.f_DTCTool = os.path.join(self.p_fsUtils, "dtc")
        self.p_BpmpDtb = os.path.join(str(self.NV_OUTDIR), "nvidia", "bpmp-dtb")
        self.p_McuUpdtScript = os.path.join(self.TEGRA_TOP, "embedded", "tacp", "scripts")
        self.p_McuFwBin = os.path.join(self.TEGRA_TOP, "embedded", "release", "utils", "rootfs_extra", "firmware", "aurix", "P3960")

        self.p_PyFlashPath = os.path.join(self.TEGRA_TOP, "embedded", "tools", "scripts", "bootburn_py")

    def SetBootBurnPaths(self):
        chipFamily = self.GetChipFamily()
        socType = self.GetSocType()
        self.getFileList(chipFamily)
        p_Debug = ""
        foundation = os.path.join(self.PDK_TOP, self.NV_SDK_NAME_FOUNDATION)
        if (self.isCreate == True):
            foundation = self.PDK_TOP
        self.GetPDKBuildVersion(foundation);

        flashtools = os.path.join(foundation, "tools", "flashtools")
        self.p_FlashPath = os.path.join(flashtools, "flash")
        self.p_FsCreationToolPath = self.p_FlashPath
        self.p_FsAndroidToolPath = self.p_FlashPath
        self.p_FsCreationmkfsPath = self.p_FlashPath
        self.p_fsUtils = self.p_FlashPath
        self.p_PyFlashPath = os.path.join(flashtools, "bootburn_py")
        self.p_FirmwareBin = os.path.join(foundation, "firmware", "bin")
        self.p_BurnStorageCfgs = os.path.abspath(os.path.join(flashtools, "storage_configs", chipFamily))
        self.p_TcumuxerSupport = os.path.join(foundation, "tools" , "muxer", "tcu_muxer")
        self.p_TcumuxerDir = self.p_TcumuxerSupport
        self.p_Mb1Headers = os.path.join(foundation, "firmware/src/bootloader/common/include/", chipFamily)
        self.p_FlashCfgPath = self.p_BurnStorageCfgs
        self.p_Hardware = os.path.join(foundation, "platform-config", "hardware", "nvidia")
        self.p_Platform = os.path.join(self.p_Hardware, "platform", chipFamily)
        self.p_PlatformConfig = os.path.join(self.p_Platform, "automotive", "automotive-platform-configs")
        self.p_boardFirmware = os.path.join(flashtools, "firmware", self.f_FirmwareJson)

        if (not self.p_OutDirPath):
            self.p_OutDirPath = os.path.join(os.getcwd(), "__temp_" + socType)

        if (sys.argv[0].find('flash_bsp_images') !=  -1) and (self.isminiPDK or (self.TEST_AUTOMATION)):
            print ("*** miniPDK -- offline binary flashing (flash_bsp)  ****")
            self.p_SocBoardConfigsPath = os.path.abspath(os.path.join(os.getcwd(), "..", "board_configs"))
        elif(self.is_l4t):
            self.p_SocBoardConfigsPath = os.path.join(flashtools, "board_configs")
        else:
            self.p_SocBoardConfigsPath = self.p_PlatformConfig

        self.p_BoardConfigsPath = self.p_SocBoardConfigsPath
        self.f_baseBoardSkuMapping = os.path.join(flashtools, "board_configs", "t264", self.f_baseBoardSkuMapping)
        self.f_chipSkuMapping = os.path.join(flashtools, "board_configs", "t264",self.f_chipSkuMapping)
        self.p_DtsIncludesJsonPath = os.path.join(flashtools, "board_configs", "t264",)
        self.f_skuInfoJson = os.path.join(flashtools, "board_configs", "t264", self.f_skuInfoJson)
        self.f_bpmpInfoJson = os.path.join(flashtools, "board_configs", "t264", self.f_bpmpInfoJson)

        # Flashing Tools
        self.flashUtils.f_AdbTool = os.path.join(self.p_FlashPath, self.flashUtils.f_AdbTool)
        self.flashUtils.f_CompressLz4 = os.path.join(self.p_FlashPath, self.flashUtils.f_CompressLz4)
        self.flashUtils.f_TegraHost = os.path.join(self.p_FlashPath, self.flashUtils.f_TegraHost)
        self.flashUtils.f_TegraBct = os.path.join(self.p_FlashPath, self.flashUtils.f_TegraBct)
        self.flashUtils.f_TegraParser = os.path.join(self.p_FlashPath, self.flashUtils.f_TegraParser)

        for index, tegrasignFile in enumerate(self.flashUtils.f_TegraSign):
            self.flashUtils.f_TegraSign[index] = os.path.join(self.p_FlashPath, tegrasignFile)

        for index, tegrasignFile in enumerate(self.flashUtils.f_TegraSignPriv):
            self.flashUtils.f_TegraSignPriv[index] = os.path.join(self.p_FlashPath, tegrasignFile)

        for index, tegrasignFile in enumerate(self.flashUtils.f_TegraSignDevKeyFiles):
            self.flashUtils.f_TegraSignDevKeyFiles[index] = os.path.join(self.p_FlashPath, tegrasignFile)

        tegraSignDebugYaml = os.path.join(self.p_FlashPath, "tegrasign_v3_debug.yaml")
        if os.path.exists(tegraSignDebugYaml):
            self.flashUtils.f_TegraSign.append(tegraSignDebugYaml)

        self.flashUtils.f_TegraRcm = os.path.join(self.p_FlashPath, self.flashUtils.f_TegraRcm)
        self.flashUtils.f_NvSkuInfo = os.path.join(self.p_FlashPath, self.flashUtils.f_NvSkuInfo)
        self.flashUtils.f_NvBchValidate = os.path.join(self.p_FlashPath, self.flashUtils.f_NvBchValidate)
        self.flashUtils.f_NvImageGen = os.path.join(self.p_FlashPath, self.flashUtils.f_NvImageGen)
        self.flashUtils.f_NvImageSign = os.path.join(self.p_FlashPath, self.flashUtils.f_NvImageSign)
        self.flashUtils.f_NvSimgDump = os.path.join(self.p_FlashPath, self.flashUtils.f_NvSimgDump)
        self.flashUtils.f_NvDDTool = os.path.join(self.p_FlashPath, self.flashUtils.f_NvDDTool)
        self.flashUtils.f_NvOptinFuse = os.path.join(self.p_FlashPath, self.flashUtils.f_NvOptinFuse)
        self.f_WrapperShellScrip = os.path.join(self.p_FlashPath, self.f_WrapperShellScrip)
        self.flashUtils.f_Nvpt = os.path.join(self.p_FlashPath, self.flashUtils.f_Nvpt)

        self.f_MB2AppletPath = os.path.join(self.p_FirmwareBin, chipFamily)

        self.f_FlashingMB2AppletPath = os.path.join(self.p_FirmwareBin, chipFamily)
        self.s_Mb2AppletMagicId = "MB2A"

        self.f_PscBlPartnerPath = os.path.join(self.PDK_TOP, "foundation", "firmware", "psc_bl1", "public-" + chipFamily)
        self.f_PscBlPrivatePath = os.path.join(self.PDK_TOP, "foundation", "firmware", "psc_bl1", "private-" + chipFamily)

        self.p_HpsePctBin = os.path.join(self.PDK_TOP, self.NV_SDK_NAME_FOUNDATION, "security", "spvault", "conf-blob", "t26x")
        self.p_HpseFindBoardPctBin = os.path.join(self.PDK_TOP, self.NV_SDK_NAME_FOUNDATION, "security", "spvault", "prebuilt", "native")
        self.p_FlashingDtbPath = os.path.join(flashtools,"flashing_kernel", "kernel", chipFamily)
        self.p_BpmpFWFlashingDtb = os.path.join(flashtools, "flashing_kernel", "flashing_fw", chipFamily)

        if (self.f_SignedCustomerDataSchema == None):
                self.f_SignedCustomerDataSchema = os.path.join(foundation, "tools", "flashtools", "bootburn_t264_py", "nv-customer-data-schema.json")

        self.p_NvGrCsvPath = os.path.join(self.p_FlashPath)
        self.p_FlashingCfgPath = os.path.join(flashtools,"flashing_kernel", "flashing_configs", chipFamily)
        if len(self.s_BoardName) != 0:
            self.p_HyperVisorCfgsPath = os.path.join(foundation, "out", self.s_BoardName)
            self.p_ClassKeyDir = os.path.join(foundation, "oout")
        self.p_BCT = os.path.join(foundation, "platform-config", "hardware", "nvidia", "platform", chipFamily)
        self.p_BpmpFwDtb = os.path.join(foundation, "platform-config", "bpmp_dt", chipFamily)
        self.p_FuseBypassPrivate = os.path.join(flashtools,"flashing_kernel", "flashing_fw", chipFamily)

        if self.p_fp_platcfg == None and len(self.s_BoardName) != 0:
            self.p_fp_platcfg = os.path.join(self.p_HyperVisorCfgsPath, "platform_config.json")

        if (self.n_FlashQnx):
            # Set the qnx dtb path for PDK environment based on the board being flashed
            self.p_StorageDtbPath = os.path.abspath(os.path.join(self.PDK_TOP, self.NV_SDK_NAME_QNX, "bsp", "images"))
            qnxOutDir = os.path.abspath(os.path.join(self.PDK_TOP, self.NV_SDK_NAME_QNX))
        else:
            # Set the native linux and hypervisor config dtb
            # path for PDK environment based on the board being flashed
            self.p_StorageDtbPath = os.path.join(self.PDK_TOP, self.NV_SDK_NAME_LINUX, "kernel/canonical-6.8/preempt_rt")

        # DriveOS internal config
        self.p_PlatformInternalBCT = os.path.join(self.p_Platform, "automotive", "automotive-platform-configs", "driveos_internal_config", "bct")
        # DriveOS customer config
        # We do not specify the path to a sub-directory within the driveos_customer_config directory because as customer can create their own platforms under this folder
        # and we don't want to hardcode the path for the customer bct
        self.p_PlatformCustomerBCT = os.path.join(self.p_Platform, "automotive", "automotive-platform-configs", "driveos_customer_config")
        self.p_PlatformSimBCT = os.path.join(self.p_Platform, "sim", "bct")
        self.p_PlatformCommonBCT = os.path.join(self.p_Platform, "common", "bct")
        self.p_PlatformSOC = os.path.join(self.p_Hardware, "soc")
        self.p_PlatformMB1Headers = os.path.join(foundation, "firmware", "src", "bootloader", "common", "include", chipFamily)
        self.p_Hwinc = os.path.join(foundation, "firmware", "src", "hwinc", "hwinc-" + chipFamily)
        self.p_LinuxStorageDtbPath = os.path.abspath(os.path.join(self.PDK_TOP, self.NV_SDK_NAME_LINUX, "kernel/canonical-6.8/preempt_rt"))
        self.p_LinuxStorageGenericDtbPath = os.path.abspath(os.path.join(self.PDK_TOP, self.NV_SDK_NAME_LINUX, "kernel/canonical-6.8/preempt_rt"))
        self.f_DTCTool = os.path.join(self.p_FlashPath, "dtc")

        self.p_BpmpDtb = os.path.join(self.PDK_TOP, self.NV_SDK_NAME_FOUNDATION, "platform-config", "bpmp_dt", "t264")
        self.p_McuUpdtScript = os.path.join(flashtools, "firmware")
        self.p_McuFwBin = os.path.join(flashtools, "firmware")  #check where exactly this is used, as the same vairable is initilized in board paths

        self.p_PyFlashPath = os.path.join(flashtools, "bootburn_py")

    def setSkuInformation(self, skuinfo):

        parser = argparse.ArgumentParser(description="Parser", add_help=False)

        for option in self.sku_options:
            arg = self.sku_options[option]
            numArguments = arg[0]
            helpMessage = arg[1]
            if numArguments > 0:
                parser.add_argument(option, help=helpMessage, nargs=numArguments)
            elif numArguments == False:
                parser.add_argument(option, help=helpMessage, action="store_true")
            else:
                parser.add_argument(option, help=helpMessage, nargs="+")

        parsed_skuinfo = parser.parse_args(skuinfo)
        parsed_skuinfo = vars(parsed_skuinfo)

        data_dict = {
            "version": 1,
            "value": None
        }

        for option in parsed_skuinfo:
            if (parsed_skuinfo[option] != None):
                if (option == "skunum"):
                    data_dict["value"] = parsed_skuinfo["skunum"][0]
                    self.customerData.setKeyValue("skuNumber", dict(data_dict))
                    self.skuArguments["s_skuarg_skunum"] = parsed_skuinfo["skunum"][0]
                elif (option == "setskuversion"):
                    self.customerData.setKeyValue("skuVersion", parsed_skuinfo["setskuversion"][0])
                    self.skuArguments["s_skuarg_setskuversion"] = parsed_skuinfo["setskuversion"][0]
                elif (option == "setprodinfo"):
                    prodInfo = parsed_skuinfo["setprodinfo"][0] + " " + parsed_skuinfo["setprodinfo"][1]
                    data_dict["value"] = prodInfo
                    self.customerData.setKeyValue("prodInfo", dict(data_dict))
                    self.skuArguments["s_skuarg_setprodinfo"] = parsed_skuinfo["setprodinfo"][0]
                elif (option == "setboardserial"):
                    data_dict["value"] = parsed_skuinfo["setboardserial"][0]
                    self.customerData.setKeyValue("boardSerial", dict(data_dict))
                    self.skuArguments["s_skuarg_setboardserial"] = parsed_skuinfo["setboardserial"][0]
                elif (option == "setmacid"):
                    macId0Data = parsed_skuinfo["setmacid"][0] + " " + parsed_skuinfo["setmacid"][1]
                    data_dict["value"] = macId0Data
                    self.customerData.setKeyValue("macId0", dict(data_dict))
                    self.skuArguments["s_skuarg_setmacid"] = parsed_skuinfo["setmacid"]
                elif(option == "setmacid0"):
                    macId0Data = parsed_skuinfo["setmacid0"][0] + " " + parsed_skuinfo["setmacid0"][1]
                    data_dict["value"] = macId0Data
                    self.customerData.setKeyValue("macId0", dict(data_dict))
                    self.skuArguments["s_skuarg_setmacid"] = parsed_skuinfo["setmacid0"]
                elif(option == "setmacid1"):
                    macId1Data = parsed_skuinfo["setmacid1"][0] + " " + parsed_skuinfo["setmacid1"][1]
                    data_dict["value"] = macId1Data
                    self.customerData.setKeyValue("macId1", dict(data_dict))
                    self.skuArguments["s_skuarg_setmacid1"] = parsed_skuinfo["setmacid1"]
                elif(option == "setmacid2"):
                    macId2Data = parsed_skuinfo["setmacid2"][0] + " " + parsed_skuinfo["setmacid2"][1]
                    data_dict["value"] = macId2Data
                    self.customerData.setKeyValue("macId2", dict(data_dict))
                    self.skuArguments["s_skuarg_setmacid2"] = parsed_skuinfo["setmacid2"]
                elif(option == "setmacid3"):
                    macId3Data = parsed_skuinfo["setmacid3"][0] + " " + parsed_skuinfo["setmacid3"][1]
                    data_dict["value"] = macId3Data
                    self.customerData.setKeyValue("macId3", dict(data_dict))
                    self.skuArguments["s_skuarg_setmacid3"] = parsed_skuinfo["setmacid3"]

    def parseDefaultValues(self, variableName, parseVariable=False):
        path = None
        if(variableName in self.boardDefaultPaths):
            path = self.boardDefaultPaths[variableName]
            if(path == None):
                return None
        elif(parseVariable):
            path = variableName
        else:
            self.boardDefaultPaths[variableName] = None
            return None
        path = path.replace("<TOP>", str(self.TOP))
        path = path.replace("<TEGRA_TOP>", str(self.TEGRA_TOP))
        path = path.replace("<NV_OUTDIR>", str(self.NV_OUTDIR))
        path = path.replace("<p_PlatformInternalBCT>", str(self.p_PlatformInternalBCT))
        path = path.replace("<p_PlatformCustomerBCT>", str(self.p_PlatformCustomerBCT))
        path = path.replace("<s_VerStr>", str(self.s_VerStr))
        path = path.replace("<l_BootDevice>", str(self.s_BootDevice))
        path = path.replace("<s_DtbImgSuffix>", str(self.s_DtbImgSuffix))
        path = path.replace("<p_BCT>", str(self.p_BCT))
        path = path.replace("<p_PlatformCommonBCT>", str(self.p_PlatformCommonBCT))
        path = path.replace("<p_PlatformSimBCT>", str(self.p_PlatformSimBCT))
        path = path.replace("<p_PlatformSOC>", str(self.p_PlatformSOC))
        path = path.replace("<p_PlatformMB1Headers>", str(self.p_PlatformMB1Headers))
        path = path.replace("<p_HyperVisorCfgsPath>", str(self.p_HyperVisorCfgsPath))
        path = path.replace("<p_Platform>", str(self.p_Platform))
        path = path.replace("<p_PlatformConfig>", str(self.p_PlatformConfig))
        # Top of Tree Bpmp Dtb
        path = path.replace("<p_BpmpDtb>", str(self.p_BpmpDtb))
        path = path.replace("<p_ClassKeyDir>", str(self.p_ClassKeyDir))
        path = path.replace("<BOARD>", str(self.s_BoardName))

        self.boardDefaultPaths[variableName] = path
        return path

    def BoardSetFilePathsAndDefaultValues(self):
        print("\n In BoardSetFilePathsAndDefaultValues in targetConfig \n")
        l_OsStr = None
        l_HypConfigTypeFile = None

        if "s_Skuname" not in self.boardDefaultPaths:
            self.boardDefaultPaths["s_Skuname"] = "TA1090SA"
        if "s_Int" not in self.boardDefaultPaths:
            self.boardDefaultPaths["s_Int"] = "F0"
        if "b_SkuCheck" not in self.boardDefaultPaths:
            self.b_SkuCheck = True
        elif (self.boardDefaultPaths["b_SkuCheck"] == str(True)):
            self.b_SkuCheck = True
        else:
            self.b_SkuCheck = False

        if (self.n_ChipVersion == 1):
            self.s_VerStr = "a01-"
        elif (self.n_ChipVersion == 2):
            self.s_VerStr = "a02-"
        elif (sys.argv[0].find('flash_bsp_images') != -1):
            self.s_VerStr = ""
        elif (not self.parsed_commandline["toolsonly"]):
            print("The chip version " + str(self.n_ChipVersion) + " is not supported")
            AbnormalTermination("Invalid chip version", nverror.NvError_InvalidArgument)

        bootDevice = self.s_BootDevice
        if(bootDevice == "sdmmc"):
            bootDevice = "emmc"
        #FW update parameters in the boardConfig
        if "n_FlashFirmware" not in self.boardDefaultPaths:
             self.n_FlashFirmware = False
             print("n_FlashFirmware is not listed in the Boardconfig")
        elif (self.boardDefaultPaths["n_FlashFirmware"] == str(True)):
             self.n_FlashFirmware = True
             print("n_FlashFirmware is set to True")
        else:
            self.n_FlashFirmware = False
            print("n_FlashFirmware is set to False")

        #Read the target port from the board Config
        if "s_tegraUsbPort" not in self.boardDefaultPaths:
            self.s_tegraUsbPort = "/dev/ttyACM0"
        else:
            self.s_tegraUsbPort = self.boardDefaultPaths["s_tegraUsbPort"]
            print ("s_tegraUsbPort in boardConfig : " + str(self.s_tegraUsbPort))
        #Atomic flashing initializing default to disable
        if ("s_BrickOnCoreFWFlashFail" not in self.boardDefaultPaths):
            self.s_BrickOnCoreFWFlashFail = False
            print("*** in targetConfig, s_BrickOnCoreFWFlashFail  not found in board_config ***")
        elif (self.boardDefaultPaths["s_BrickOnCoreFWFlashFail"] == str(True)):
            self.s_BrickOnCoreFWFlashFail = True
        else:
            self.s_BrickOnCoreFWFlashFail = False
        if ("s_BrickOnFirmwareUpdateFail" not in self.boardDefaultPaths):
            self.boardDefaultPaths["s_BrickOnFirmwareUpdateFail"] = "False"
            print("*** in targetConfig, s_BrickOnFirmwareUpdateFail  not found in board_config ***")
        elif (self.boardDefaultPaths["s_BrickOnFirmwareUpdateFail"] == str(True)):
            self.s_BrickOnFirmwareUpdateFail = True
        else:
            self.s_BrickOnFirmwareUpdateFail = False

        if ("s_FlashingUsbVersion" in self.boardDefaultPaths):
            self.s_FlashingUsbVersion = self.boardDefaultPaths["s_FlashingUsbVersion"]
        else:
            self.s_FlashingUsbVersion = "3"

        if ("f_SignedCustomerData" in self.boardDefaultPaths):
            self.parseDefaultValues("f_SignedCustomerData")
            self.f_SignedCustomerData = self.boardDefaultPaths["f_SignedCustomerData"]
            if (not os.path.isfile(self.f_SignedCustomerData)):
                if (self.isPDK == True) and (self.BOOTBURN_PYTEST == False):
                    board_configs = "../board_configs"
                else:
                    board_configs = "../../board_configs"
                f_SignedCustomerData = os.path.join(os.getcwd(), board_configs, self.f_SignedCustomerData)
                if os.path.isfile(f_SignedCustomerData):
                    self.f_SignedCustomerData = f_SignedCustomerData
                elif(self.is_l4t or self.is_sim):
                    f_SignedCustomerData = os.path.join(self.self.p_SocBoardConfigsPath, self.f_SignedCustomerData)
                else:
                    searchPath = self.p_SocBoardConfigsPath
                    f_SignedCustomerData = self.shellUtils.find(searchPath, self.f_SignedCustomerData)
                    if(len(f_SignedCustomerData) > 0):
                        f_SignedCustomerData = f_SignedCustomerData[0]

                if os.path.isfile(f_SignedCustomerData):
                    self.f_SignedCustomerData = f_SignedCustomerData
                else:
                    AbnormalTermination("Can't find Signed Customer Data File " + self.f_SignedCustomerData, nverror.NvError_BadParameter)


        if(self.parsed_commandline and self.parsed_commandline["P"]):
            return

        self.parseDefaultValues("f_NvQbDtbImage")
        self.parseDefaultValues("f_MB1Pad")
        self.parseDefaultValues("f_MB1Pinmux")
        self.parseDefaultValues("f_MB1Prod")
        self.parseDefaultValues("f_MB1Pmic")
        self.parseDefaultValues("f_MB1Misc")
        self.parseDefaultValues("f_MB1Softfuses")
        self.parseDefaultValues("f_MB1GpioInt")
        self.parseDefaultValues("f_MB1BootDevice")
        self.parseDefaultValues("f_FlashingMB1SdRamParam")
        self.parseDefaultValues("f_MB1SdRamParam")
        self.parseDefaultValues("f_MB1wb0SdRamParam")
        self.parseDefaultValues("f_MB1DramECCMisc")
        self.parseDefaultValues("f_MB1SdRamSwOverride_mods")
        self.parseDefaultValues("f_MB1SdRamSwOverride")
        self.parseDefaultValues("f_MB1Scr_mods")
        self.parseDefaultValues("f_MB2Scr_mods")
        self.parseDefaultValues("f_MB1Scr_safety")
        self.parseDefaultValues("f_MB2Scr_safety")
        self.parseDefaultValues("f_MB1Scr")
        self.parseDefaultValues("f_MB2Scr")
        self.parseDefaultValues("f_MB1FlashingScr")
        self.parseDefaultValues("f_MB2FlashingScr")
        if (("s_Kernel_Dtb_Variant" in self.boardDefaultPaths) and (isinstance(self.boardDefaultPaths["s_Kernel_Dtb_Variant"], list))):
            self.KernelDtbList = self.boardDefaultPaths["s_Kernel_Dtb_Variant"]
            for value in self.KernelDtbList:
                index = self.KernelDtbList.index(value)
                if index == 0:
                    name = "s_Kernel_Dtb_Variant"
                else:
                    name = "s_Kernel_Dtb_Variant" + str(index)
                self.boardDefaultPaths[name] = value
        else:
            self.parseDefaultValues("s_Kernel_Dtb_Variant")
        #self.parseDefaultValues("s_ChainC_Kernel_Dtb_Variant")
        self.parseDefaultValues("f_DtbImg")
        self.parseDefaultValues("f_FlashingDtbImg")
        self.parseDefaultValues("f_BpmpFwDtb")
        self.parseDefaultValues("f_FlashingBpmpFwDtb")
        self.parseDefaultValues("f_MB1SdRamParamEcc")
        self.parseDefaultValues("f_UFSPhyLaneFile")
        self.parseDefaultValues("s_GrCsvString")
        self.parseDefaultValues("s_BoardId")
        self.parseDefaultValues("f_MB1MinRatchet")
        self.parseDefaultValues("f_MB1DeviceProd")
        self.parseDefaultValues("f_SafeBpmpFwDtb")
        if (("s_BPMP_Dtb_Variant" in self.boardDefaultPaths) and (isinstance(self.boardDefaultPaths["s_BPMP_Dtb_Variant"], list))):
            self.BpmpFwDtbList = self.boardDefaultPaths["s_BPMP_Dtb_Variant"]
            for value in self.BpmpFwDtbList:
                index = self.BpmpFwDtbList.index(value)
                if index == 0:
                    name = "s_BPMP_Dtb_Variant"
                else:
                    name = "s_BPMP_Dtb_Variant" + str(index)
                self.boardDefaultPaths[name] = value
        else:
            self.parseDefaultValues("s_BPMP_Dtb_Variant")
        self.parseDefaultValues("s_BPMP_Dtb_Variant")
        self.parseDefaultValues("s_IST_Ict_Variant")
        self.parseDefaultValues("n_DramOverride")
        self.parseDefaultValues("f_BrBctDevParam")
        self.parseDefaultValues("f_BootromCfg")
        self.parseDefaultValues("f_MB2Bct")
        self.parseDefaultValues("f_HpseBrBctDevParam")
        self.parseDefaultValues("f_HpseMB1SdRamParam")
        self.parseDefaultValues("f_HpseMB1wb0SdRamParam")
        self.parseDefaultValues("f_MB2Bin")
        self.parseDefaultValues("f_SbBrBctDevParam")
        self.parseDefaultValues("f_SbMB1SdRamParam")
        if 'f_BpmpMemCfgParam' in self.boardDefaultPaths:
            self.parseDefaultValues("f_BpmpMemCfgParam")
        if 'f_MB1MultiSkuPlatformParam' in self.boardDefaultPaths:
            self.parseDefaultValues("f_MB1MultiSkuPlatformParam")

        # Default for Qspi
        if ("f_MB2StorageDevices" not in self.boardDefaultPaths):
            self.boardDefaultPaths["f_MB2StorageDevices"] = "<p_PlatformSimBCT>/device/dummy_storage.xml"
        self.parseDefaultValues("f_MB2StorageDevices")
        if("f_UFSDebugfsNode" in self.boardDefaultPaths):
            self.parseDefaultValues("f_UFSDebugfsNode")
        else:
            self.boardDefaultPaths["f_UFSDebugfsNode"] = self.s_UFS_debugfs_node

        if ("s_FuseBypass" in self.boardDefaultPaths):
            self.parseDefaultValues("s_FuseBypass")
        else:
            self.boardDefaultPaths["s_FuseBypass"] = ""

        if (self.f_CustomFlashCfg and self.n_FlashHypervisor):
            self.f_FlashCfg = self.f_CustomFlashCfg
            l_HypConfigTypeFile = os.path.join(self.p_HyperVisorCfgsPath, "pct_type.txt")
            self.s_HypConfig = self.shellUtils.catFile(l_HypConfigTypeFile)
            self.s_HypConfig = self.s_HypConfig.rstrip()
        elif (self.f_CustomFlashCfg):
            self.f_FlashCfg = self.f_CustomFlashCfg
        elif (self.n_FlashHypervisor):
            if(os.path.isdir(self.p_HyperVisorCfgsPath) == False):
                msg = "Bind partitions does not look to have been run for board " + self.s_BoardName + "\n"
                msg += "please make bind  partitions before running bootburn"
                AbnormalTermination(msg, nverror.NvError_BadParameter)
            self.f_FlashCfg = os.path.join(self.p_HyperVisorCfgsPath, "global_storage.cfg")
            l_HypConfigTypeFile = os.path.join(self.p_HyperVisorCfgsPath, "pct_type.txt")
            self.s_HypConfig = self.shellUtils.catFile(l_HypConfigTypeFile)
            self.s_HypConfig = self.s_HypConfig.rstrip()
        else:
            if (self.n_FlashQnx):
                l_OsStr = "qnx"
            elif (self.n_FlashLinux):
                if (not self.isPDK and self.n_KernelVariantRT):
                    l_OsStr = "rt_linux"
                else:
                    l_OsStr = "linux"

                if (not self.isPDK):
                    l_OsStr = l_OsStr + "_initramfs"

                if (self.parsed_commandline["vdk"] and self.GetChipFamily() == "t264"):
                    if (self.TEST_AUTOMATION == True):
                        l_OsStr = l_OsStr + "_test_automation_vdk"
                    else:
                        l_OsStr = l_OsStr + "_vdk"

            else:
                AbnormalTermination("No valid native OS is specified", nverror.NvError_InvalidArgument)

            if(self.f_CustomFlashCfg != None):
                self.f_FlashCfg = self.f_CustomFlashCfg
            else:
                self.f_FlashCfg = os.path.join(self.p_FlashCfgPath, "quickboot_" + bootDevice + "_" + l_OsStr + ".cfg")

        if(self.s_HypConfig != None):
            if(isinstance(self.s_HypConfig, str) != True):
                self.s_HypConfig = str(self.s_HypConfig)

        if (self.n_FlashHypervisor):
            self.p_autogenPath = os.path.join(self.p_HyperVisorCfgsPath, "pct", self.s_HypConfig, "autogen")

        if (os.path.isfile(self.f_FlashCfg) == False):
            if (self.n_FlashHypervisor):
                self.f_FlashCfg = os.path.join(self.p_HyperVisorCfgsPath, "global_storage_" + bootDevice + ".cfg")

        if (os.path.isfile(self.f_FlashCfg) == False):
            print("Config file not present at " + self.f_FlashCfg)
            if (self.n_FlashHypervisor):
                print("Please execute bind_partitions to generate hypervisor images before flashing!")
                AbnormalTermination("Config file not found", nverror.NvError_InvalidArgument)

        #need to update fw paths here, as they dependent on the OS being used.
        # the OS is detected in the above part.
        if(self.PDK_TOP) and (self.s_HypConfig != None):
            if "qnx" in self.s_HypConfig:
                self.p_McuFwBin = os.path.join(self.PDK_TOP, self.NV_SDK_NAME_QNX, "aurix")
            else:
                self.p_McuFwBin = os.path.join(self.PDK_TOP, self.NV_SDK_NAME_LINUX, "aurix")

        self.BoardSetMB1MiscPathForThermal()

    def SetFlash_bsp_Paths(self):
        if (sys.argv[0].find('flash_bsp_images') !=  -1) and (not self.isminiPDK) and (not self.TEST_AUTOMATION):
            return

        baseboardSkuMapping = os.path.basename(self.f_baseBoardSkuMapping)
        chipSkuMapping = os.path.basename(self.f_chipSkuMapping)
        self.f_baseBoardSkuMapping = os.path.join(os.getcwd(),"../board_configs", baseboardSkuMapping)
        self.f_chipSkuMapping = os.path.join(os.getcwd(),"../board_configs", chipSkuMapping)

        if (self.f_SignedCustomerData == None) and ("f_SignedCustomerData" in self.boardDefaultPaths):
            self.parseDefaultValues("f_SignedCustomerData")
            self.f_SignedCustomerData = self.boardDefaultPaths["f_SignedCustomerData"]
            if (not os.path.isfile(self.f_SignedCustomerData)):
                if (self.isPDK == True) and (self.BOOTBURN_PYTEST == False):
                    board_configs = "../board_configs"
                else:
                    board_configs = "../../board_configs"
            self.f_SignedCustomerData = os.path.join(os.getcwd(), board_configs, self.f_SignedCustomerData)


    def BoardSetMB1MiscPathForThermal(self):
        if ("power_modes" in self.boardDefaultPaths):
            power_modes = self.boardDefaultPaths["power_modes"]
        else:
            power_modes = {}

        #Override NV_BUILD_WORK_LOAD if we are in safety and it is not set
        if (self.SAFETY_BUILD) and (self.NV_BUILD_WORK_LOAD == None):
            if ("s_Sku" in power_modes):
                s_Sku = power_modes["s_Sku"]
            else:
                s_Sku = "TA890SA"
            if (s_Sku == "TA890SA"):
                self.NV_BUILD_WORK_LOAD = "TA890SA_MAXP_APOLLO"
            else:
                self.NV_BUILD_WORK_LOAD = "TA892SA_MAXP_L2"

        if self.NV_BUILD_WORK_LOAD != None:
            if (self.NV_BUILD_WORK_LOAD in power_modes):
                path =  power_modes[self.NV_BUILD_WORK_LOAD]
                path = path.replace("<p_BCT>", str(self.p_BCT))
                self.boardDefaultPaths["f_MB1Misc"] = path

    def getOutDir(self, baseBuildType, targetBoard, buildFlavor, configVariant=None):

        topDir = self.TEGRA_TOP
        baseBuildType = baseBuildType.strip()
        safetyDir = ""

        if (self.witheenv == True) and (baseBuildType == "foundation"):
            return [self.NV_OUTDIR, topDir, safetyDir]

        if (os.path.isdir(os.path.join(topDir, "..", baseBuildType, "out"))):
            topDir = os.path.abspath(os.path.join(topDir, "..", baseBuildType))

        outDir = os.path.join(topDir, "out", "embedded-" + baseBuildType + "-" + targetBoard + "-" + buildFlavor)
        if(baseBuildType == "qnx" or baseBuildType == "foundation"):
            outDir = outDir + "-" + configVariant
        elif (baseBuildType == "linux"):
            if(self.TEST_AUTOMATION):
                outDir = os.path.join(topDir, "../")
                topDir = os.path.join(topDir, "../")
                safetyDir = os.path.join(topDir, "../")
            elif configVariant == "safety":
                saveDir = outDir
                outDir = saveDir + "-" + "none"
                safetyDir = saveDir + "-" + "safety"
            else:
                outDir = outDir + "-" + configVariant

        return [outDir, topDir, safetyDir]
