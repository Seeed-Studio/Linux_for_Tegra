#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION, its affiliates and licensors retain all intellectual
# property and proprietary rights in and to this material, related
# documentation and any modifications thereto. Any use, reproduction,
# disclosure or distribution of this material and related documentation
# without an express license agreement from NVIDIA CORPORATION or
# its affiliates is strictly prohibited.

import traceback
import inspect
import sys

class nverror(object):
    NvError_Success = 0x00000000
    NvError_NotImplemented = 0x00000001
    NvError_NotSupported = 0x00000002
    NvError_NotInitialized = 0x00000003
    NvError_BadParameter = 0x00000004
    NvError_Timeout = 0x00000005
    NvError_InsufficientMemory = 0x00000006
    NvError_ReadOnlyAttribute = 0x00000007
    NvError_InvalidState = 0x00000008
    NvError_InvalidAddress = 0x00000009
    NvError_InvalidSize = 0x0000000A
    NvError_BadValue = 0x0000000B
    NvError_AlreadyAllocated = 0x0000000C
    NvError_Busy = 0x0000000D
    NvError_ModuleNotPresent = 0x0000000E
    NvError_ResourceError = 0x0000000F
    NvError_CountMismatch = 0x00000010
    NvError_OverFlow = 0x00000011
    NvError_ImageCorrupted = 0x00000012
    NvError_BadImage = 0x00000013
    NvError_FuseBurningTempReadFailure = 0x00000014
    NvError_Mb1PartialUpdate = 0x00000015
    NvError_Sc7PartialUpdate = 0x00000016
    NvError_MtsPartialUpdate = 0x00000017
    NvError_Mb1OemSwRatchetSanityCheckFailed = 0x00000018
    NvError_MtsOemSwRatchetSanityCheckFailed = 0x00000019
    NvError_MtsRatchetScratchNotInitialized = 0x0000001A
    NvError_Sc7OemSwRatchetSanityCheckFailed = 0x0000001B
    NvError_Mb1Sc7OemSwRatchetMismatch = 0x0000001C
    NvError_HashMismatch = 0x0000001D
    NvError_ImageMismatch = 0x0000001E
    Nverror_InvalidPtLayout = 0x0000001F
    NvError_BCHNotCached = 0x00000020
    NvError_FuseBurningTempLow = 0x00000021
    NvError_FuseBurningTempHigh = 0x00000022
    NvError_ECIDMisMatch = 0x00000023
    NvError_InActiveBinInvalid = 0x00000024
    NvError_BomDataBoardMapFileNotFound = 0x00000025
    NvError_TargetValidationFail = 0x00000026
    NvError_DevBinariesonProdTarget = 0x00000027
    NvError_ProdBinariesonDevTarget = 0x00000028
    NvError_TargetComError = 0x00000029
    NvError_TegraBctError = 0x0000002A
    NvError_TegraRcmError = 0x0000002B
    NvError_TegraSkuInfoError = 0x0000002C
    NvError_TegraSignError = 0x0000002D
    NvError_NvImagegen = 0x0000002E
    NvError_DeviceFailToBoot = 0x0000002F
    NvError_SystemCommand = 0x00000030
    NvError_RatchetConfigNotFound = 0x00000031
    NvError_AdbPull = 0x00000032
    NvError_AdbPush = 0x00000033
    NvError_AdbShell = 0x00000034
    NvError_Adb = 0x00000035
    NvError_Python = 0x00000036
    NvError_Bad_MD5 = 0x00000037
    NvError_InsufficientHostSpace = 0x00000038
    NvError_TegraParserError = 0x00000039
    NvError_TegraUfsProvisionError = 0x0000003A
    NvError_FileWriteFailed = 0x0000003B
    NvError_FileReadFailed = 0x0000003C
    NvError_EndOfFile = 0x0000003D
    NvError_FileOperationFailed = 0x0000003F
    NvError_OperationNotPermitted = 0x00000040
    NvError_DirOperationFailed = 0x00000041
    NvError_EndOfDirList = 0x00000042
    NvError_ConfigVarNotFound = 0x00000043
    NvError_InvalidConfigVar = 0x00000044
    NvError_MemoryMapFailed = 0x00000045
    NvError_IoctlFailed = 0x00000046
    NvError_AccessDenied = 0x00000047
    NvError_DeviceNotFound = 0x00000048
    NvError_KernelDriverNotFound = 0x00000049
    NvError_FileNotFound = 0x0000004A
    NvError_InvalidArgument = 0x0000004B
    NvError_ProcessNotFound = 0x0000004C
    NvError_Deadlock = 0x0000004D
    NvError_FileNameNotExist = 0x0000004E
    NvError_PartitionNotExist = 0x0000004F
    NvError_DeviceFailToRegister = 0x00000050
    NvError_SystemCommandFailed = 0x00000051
    NvError_CorruptedBuffer = 0x00000052
    NvError_SdioCardNotPresent = 0x00000053
    NvError_SdioInstanceTaken = 0x00000054
    NvError_SdioControllerBusy = 0x00000055
    NvError_SdioReadFailed = 0x00000056
    NvError_SdioWriteFailed = 0x00000057
    NvError_SdioBadBlockSize = 0x00000058
    NvError_SdioClockNotConfigured = 0x00000059
    NvError_SdioSdhcPatternIntegrityFailed = 0x0000005A
    NvError_SdioCommandFailed = 0x0000005B
    NvError_SdioCardAlwaysPresent = 0x0000005C
    NvError_SdioAutoDetectCard = 0x0000005D
    NvError_SdMmcRecoveryFailed = 0x0000005E
    NvError_SdMmcTransferStateTimeout = 0x0000005F
    NvError_SdMmcStandByStateTimeout = 0x00000060
    NvError_SdMmcIdleStateTimeout = 0x00000061
    NvError_I2cReadFailed = 0x00000062
    NvError_I2cWriteFailed = 0x00000063
    NvError_I2cDeviceNotFound = 0x00000064
    NvError_I2cInternalError = 0x00000065
    NvError_I2cArbitrationFailed = 0x00000066
    NvError_I2CCommunicationError = 0x00000067
    NvError_IdeHwError = 0x00000068
    NvError_IdeReadError = 0x00000069
    NvError_IdeWriteError = 0x0000006A
    NvError_VsWriteError = 0x0000006B
    NvError_VsReadError = 0x0000006C
    NvError_ChipSkuClockLimitViolation = 0x0000006D
    NvError_ChipSkuNumCoreViolation = 0x0000006E
    NvError_ChipSkuDmaFuseReadFail = 0x0000006F
    NvError_ChipSkuNoTableEntry = 0x00000070
    NvError_ChipSkuInvalidDividerValue = 0x00000071
    NvError_MCMRevFuseReadFail = 0x00000072
    NvError_NvImageSign = 0x00000073
    NvError_FirmwareVersionCheckFail = 0x00000074
    NvError_NoFirmwareFlashNeeded = 0x00000075
    NvError_FirmwareFlashFail = 0x00000076
    NvError_FirmwareConfigReadFail = 0x00000077
    NvError_FindBoardNameonFusedBoard = 0x00000078
    NvError_NoFirmwareAvailable = 0x00000079
    NvError_Unknown = 0x0000007A

    def __init__(self):

        self.NvErrorString = {
            self.NvError_Success:                          "Success" ,
            self.NvError_NotImplemented:                   "NotImplemented" ,
            self.NvError_NotSupported:                     "NotSupported" ,
            self.NvError_NotInitialized:                   "NotInitialized" ,
            self.NvError_BadParameter:                     "BadParameter" ,
            self.NvError_Timeout:                          "Timeout" ,
            self.NvError_InsufficientMemory:               "InsufficientMemory" ,
            self.NvError_ReadOnlyAttribute:                "ReadOnlyAttribute" ,
            self.NvError_InvalidState:                     "InvalidState" ,
            self.NvError_InvalidAddress:                   "InvalidAddress" ,
            self.NvError_InvalidSize:                      "InvalidSize" ,
            self.NvError_BadValue:                         "BadValue" ,
            self.NvError_AlreadyAllocated:                 "AlreadyAllocated" ,
            self.NvError_Busy:                             "Busy" ,
            self.NvError_ModuleNotPresent:                 "ModuleNotPresent" ,
            self.NvError_ResourceError:                    "ResourceError" ,
            self.NvError_CountMismatch:                    "CountMismatch" ,
            self.NvError_OverFlow:                         "OverFlow" ,
            self.NvError_ImageCorrupted:                   "ImageCorrupted" ,
            self.NvError_BadImage:                         "BadImage" ,
            self.NvError_FuseBurningTempReadFailure:       "FuseBurningTemperatureReadFailure" ,
            self.NvError_Mb1PartialUpdate:                 "Mb1PartialUpdate" ,
            self.NvError_Sc7PartialUpdate:                 "Sc7PartialUpdate" ,
            self.NvError_MtsPartialUpdate:                 "MtsPartialUpdate" ,
            self.NvError_Mb1OemSwRatchetSanityCheckFailed: "Mb1OemSwRatchetSanityCheckFailed" ,
            self.NvError_MtsOemSwRatchetSanityCheckFailed: "MtsOemSwRatchetSanityCheckFailed" ,
            self.NvError_MtsRatchetScratchNotInitialized:  "MtsRatchetScratchNotInitialized" ,
            self.NvError_Sc7OemSwRatchetSanityCheckFailed: "Sc7OemSwRatchetSanityCheckFailed" ,
            self.NvError_Mb1Sc7OemSwRatchetMismatch:       "Mb1Sc7OemSwRatchetMismatch" ,
            self.NvError_HashMismatch:                     "HashMismatch" ,
            self.NvError_ImageMismatch:                    "ImageMismatch" ,
            self.Nverror_InvalidPtLayout:                  "InvalidPtLayout" ,
            self.NvError_BCHNotCached:                     "BCHNotCached" ,
            self.NvError_FuseBurningTempLow:               "FuseBurningTemperatureLow" ,
            self.NvError_FuseBurningTempHigh:              "FuseBurningTemperatureHigh" ,
            self.NvError_ECIDMisMatch:                     "ECID MisMatch",
            self.NvError_InActiveBinInvalid:               "NvError_InActiveBinInvalid" ,
            self.NvError_BomDataBoardMapFileNotFound:	   "BomDataBoardMapFileNotFound" ,
            self.NvError_TargetValidationFail:              "TargetValidationFail" ,
            self.NvError_FileWriteFailed:                  "FileWriteFailed" ,
            self.NvError_FileReadFailed:                   "FileReadFailed" ,
            self.NvError_EndOfFile:                        "EndOfFile" ,
            self.NvError_FileOperationFailed:              "FileOperationFailed" ,
            self.NvError_OperationNotPermitted:            "OperationNotPermitted" ,
            self.NvError_DirOperationFailed:               "DirOperationFailed" ,
            self.NvError_EndOfDirList:                     "EndOfDirList" ,
            self.NvError_ConfigVarNotFound:                "ConfigVarNotFound" ,
            self.NvError_InvalidConfigVar:                 "InvalidConfigVar" ,
            self.NvError_MemoryMapFailed:                  "MemoryMapFailed" ,
            self.NvError_IoctlFailed:                      "IoctlFailed" ,
            self.NvError_AccessDenied:                     "AccessDenied" ,
            self.NvError_DeviceNotFound:                   "DeviceNotFound" ,
            self.NvError_KernelDriverNotFound:             "KernelDriverNotFound" ,
            self.NvError_FileNotFound:                     "FileNotFound" ,
            self.NvError_InvalidArgument:                  "InvalidArgument" ,
            self.NvError_ProcessNotFound:                  "ProcessNotFound" ,
            self.NvError_Deadlock:                         "Deadlock" ,
            self.NvError_FileNameNotExist:                 "FileNameNotExist" ,
            self.NvError_PartitionNotExist:                "PartitionNotExist" ,
            self.NvError_DeviceFailToRegister:             "DeviceFailToRegister" ,
            self.NvError_SystemCommandFailed:              "SystemCommandFailed" ,
            self.NvError_CorruptedBuffer:                  "CorruptedBuffer" ,
            self.NvError_I2cReadFailed:                    "I2cReadFailed" ,
            self.NvError_I2cWriteFailed:                   "I2cWriteFailed" ,
            self.NvError_I2cDeviceNotFound:                "I2cDeviceNotFound" ,
            self.NvError_I2cInternalError:                 "I2cInternalError" ,
            self.NvError_I2cArbitrationFailed:             "I2cArbitrationFailed" ,
            self.NvError_I2CCommunicationError:            "I2CCommunicationError" ,
            self.NvError_IdeHwError:                       "IdeHwError" ,
            self.NvError_IdeReadError:                     "IdeReadError" ,
            self.NvError_IdeWriteError:                    "IdeWriteError" ,
            self.NvError_VsWriteError:                     "VsWriteError" ,
            self.NvError_VsReadError:                      "VsReadError" ,
            self.NvError_SdioCardNotPresent:               "SdioCardNotPresent" ,
            self.NvError_SdioInstanceTaken:                "SdioInstanceTaken" ,
            self.NvError_SdioControllerBusy:               "SdioControllerBusy" ,
            self.NvError_SdioReadFailed:                   "SdioReadFailed" ,
            self.NvError_SdioWriteFailed:                  "SdioWriteFailed" ,
            self.NvError_SdioBadBlockSize:                 "SdioBadBlockSize" ,
            self.NvError_SdioClockNotConfigured:           "SdioClockNotConfigured" ,
            self.NvError_SdioSdhcPatternIntegrityFailed:   "SdioSdhcPatternIntegrityFailed" ,
            self.NvError_SdioCommandFailed:                "SdioCommandFailed" ,
            self.NvError_SdioCardAlwaysPresent:            "SdioCardAlwaysPresent" ,
            self.NvError_SdioAutoDetectCard:               "SdioAutoDetectCard" ,
            self.NvError_SdMmcRecoveryFailed:              "SdMmcRecoveryFailed" ,
            self.NvError_SdMmcTransferStateTimeout:        "SdMmcTransferStateTimeout" ,
            self.NvError_SdMmcStandByStateTimeout:         "SdMmcStandByStateTimeout",
            self.NvError_SdMmcIdleStateTimeout:            "SdMmcIdleStateTimeout" ,
            self.NvError_ChipSkuClockLimitViolation:       "ChipSkuClockLimitViolation" ,
            self.NvError_ChipSkuNumCoreViolation:          "ChipSkuNumCoreViolation" ,
            self.NvError_ChipSkuDmaFuseReadFail:           "ChipSkuDmaFuseReadFail" ,
            self.NvError_ChipSkuNoTableEntry:              "ChipSkuNoTableEntry" ,
            self.NvError_ChipSkuInvalidDividerValue:       "ChipSkuInvalidDividerValue" ,
            self.NvError_MCMRevFuseReadFail:               "MCMRevFuseReadFail" ,
            self.NvError_Bad_MD5:                          "Bad Md5 calculation",
            self.NvError_Python:                           "Python error",
            self.NvError_InsufficientHostSpace:            "InsufficientHostSpace",
            self.NvError_FindBoardNameonFusedBoard:        "FindBoardNameonFusedBoardNotSupported",
            self.NvError_FirmwareVersionCheckFail:         "FirmwareVersionCheckFail",
            self.NvError_NoFirmwareFlashNeeded:            "FirmwareFlashNotNeeded",
            self.NvError_FirmwareFlashFail:                "FirmwareFlashFail",
            selfNvError_FindBoardNameonFusedBoard:         "Find board is not supported on Secure fused Board",
            self.NvError_NoFirmwareAvailable:              "No Firmware Available",
            self.NvError_Unknown:                          "Unknown"
            }

# terminate operation abnormally
# argument 1 contains string for error code
# argument 2 contains integer code
def AbnormalTermination(errorDescription="Unknown Error", errorCode=-1, returnExitCode=False):

    print("command line used was:")
    print(sys.argv)
    print("\n")

    if(returnExitCode):
        sys.exit(errorCode)
    else:
        print("\033[01;31m" + str(errorDescription) + "\033[0m\n")

        raise OSError(errorCode)
