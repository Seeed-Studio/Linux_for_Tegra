#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2017-2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

# fd used for input streams
# 7 - reading FileToFlash.txt

import os
import time
import inspect
import filecmp
import hashlib
import glob
from multiprocessing import Process, Queue
import tempfile
import re

from target_config import target_config
from flashtools_nverror import nverror
from flashtools_nverror import AbnormalTermination
from flash_utilities import flash_utilities
from flash_utilities import shell_utilities
from asymmetric_boot_chains import AsymmetricFlashing
import struct


class partitionInformation(object):
    # information structure for line of FileToFlash
    # An example entry of the File to flash is given below
    # for reference
    #
    # LinuxPartitionName, PartitionName, FileName, Start, Size, BlockCount, Resize, sku_dependent, BchPartitionName, ImageHeaderType
    # /dev/block/3270000.spi A_mb1-bootloader 2_mb1_zerosign.bin 524288 262144 1 0 0 A_mb1-bootloader 9
    LinuxPartitionName = None
    PartitionName = None
    FileName = None
    Start = 0
    Size = 0
    BlockCount = 0
    Resize = False
    sku_dependent = False
    BchPartitionName = None
    ImageHeaderType = 0
    md5 = None
    ReadWrite = False

class parttableInformation(object):
    # information structure for line of bct.txt or pt*.txt
    #
    # LinuxPartitionName, Start, Size
    # /dev/block/3270000.spi 1048576 524288
    LinuxPartitionName = None
    Start = 0
    Size = 0

class ptInformation(object):
    # information structure for line of bct.txt or pt*.txt
    #
    # PartitionId Name DeviceId DeviceInstance Start End IsPersistent
    # 1 bct 2 0 0 524287 0
    name = None
    device = 0
    instance = 0
    Start = 0
    End = 0
    IsPersistent = 0

class bootburn_adb(object):
    targetConfig = target_config
    flashUtils = flash_utilities
    shellUtils = shell_utilities()
    f_AdbTool = "adb"
    n_ShellExecTotalTime = 0
    partitionInfoList = []
    s_sdmmc_device_name_list=["/dev/block/3400000.sdhci", "/dev/block/3440000.sdhci", "/dev/block/3460000.sdhci"]
    s_qspi_device_name_list = ["/dev/block/810c5b0000.spi", "/dev/block/1810c5b0000.spi"]
    s_scsi_device_name_re = r"\/dev\/sd[a-z]+" # Regular expression to match hard drives like /dev/sda, /dev/sdb, etc.
    s_nvme_device_name_re = r"\/dev\/nvme[0-9]+n[0-9]+" # Regular expression to match NVMe devices like /dev/nvme0n1, /dev/nvme1n1, etc.
    s_emmc_device_name_re = r"\/dev\/(block\/)?mmcblk[0-9]+" # Regular expression to match eMMC devices like /dev/mmcblk0, /dev/block/mmcblk0, etc.
    s_ufs_device_name_re = r"\/dev\/(block\/)?[0-9a-f]+[0-9a-f]+0000.ufshci" # Regular expression to match UFS devices like /dev/block/810c5b0000.ufshci
    s_external_storage_device_name_re = { "eMMC": s_emmc_device_name_re, "SCSI": s_scsi_device_name_re, "NVMe": s_nvme_device_name_re, "UFS": s_ufs_device_name_re}

    def __init__(self, tConfig, futils):
        self.targetConfig = tConfig
        self.flashUtils = futils
        self.f_AdbTool = self.flashUtils.f_AdbTool
        self.shellUtils.setMonitor(self.targetConfig.sysMonitor)
        self.shellUtils.exceptlog = True
        self.expandedPart = None

    def log(self, msg):
        curframe = inspect.currentframe()
        stack = inspect.getouterframes(curframe, 2)
        caller = stack[1][3]
        lineNum = stack[1][2]
        self.targetConfig.sysMonitor.log("[" + caller + "(" + str(lineNum) + ")] : " + msg, False)

    def vlog(self, msg):
        curframe = inspect.currentframe()
        stack = inspect.getouterframes(curframe, 2)
        caller = stack[1][3]
        lineNum = stack[1][2]
        self.targetConfig.sysMonitor.log("[" + caller + "(" + str(lineNum) + ")] : " + msg, False)

    def parseFileToFlashEntry(self, fileToFlashEntry):
        fileToFlashEntry = fileToFlashEntry.rstrip()
        fileToFlashEntry = fileToFlashEntry.split(' ')

        partitionInfo = partitionInformation()
        partitionInfo.LinuxPartitionName = fileToFlashEntry[0]
        partitionInfo.PartitionName = fileToFlashEntry[1]
        partitionInfo.FileName = fileToFlashEntry[2]
        partitionInfo.Start = fileToFlashEntry[3]
        partitionInfo.Size = fileToFlashEntry[4]
        partitionInfo.BlockCount = fileToFlashEntry[5]
        partitionInfo.Resize = fileToFlashEntry[6]
        partitionInfo.sku_dependent = fileToFlashEntry[7]
        partitionInfo.BchPartitionName = fileToFlashEntry[8]
        partitionInfo.ImageHeaderType = fileToFlashEntry[9]
        partitionInfo.md5 = fileToFlashEntry[10]
        partitionInfo.ReadWrite = fileToFlashEntry[11]

        return partitionInfo

    def printFileToFlashEntry(self, partitionInfo):
        self.vlog("partitionInfo.LinuxPartitionName = " + str(partitionInfo.LinuxPartitionName))
        self.vlog("partitionInfo.PartitionName = " + str(partitionInfo.PartitionName))
        self.vlog("partitionInfo.FileName = " + str(partitionInfo.FileName ))
        self.vlog("partitionInfo.Start = " + str(partitionInfo.Start))
        self.vlog("partitionInfo.Size = " + str(partitionInfo.Size))
        self.vlog("partitionInfo.BlockCount = " + str(partitionInfo.BlockCount))
        self.vlog("partitionInfo.Resize = " + str(partitionInfo.Resize))
        self.vlog("partitionInfo.sku_dependent = " + str(partitionInfo.sku_dependent))
        self.vlog("partitionInfo.BchPartitionName = " + str(partitionInfo.BchPartitionName))
        self.vlog("partitionInfo.ImageHeaderType = " + str(partitionInfo.ImageHeaderType))
        self.vlog("partitionInfo.md5 = " + str(partitionInfo.md5))
        self.vlog("partitionInfo.ReadWrite = " + str(partitionInfo.ReadWrite))

    # generate parititionInfoList from FileToFlash entries
    def generatePartitionEntryData(self, fileToFlash, flashPath):
        l_numPartitions = 0

        cwd = os.getcwd()
        os.chdir(os.path.join(flashPath))

        with open(fileToFlash, 'r') as fd:
            partitionData = fd.read()

        partitionData = partitionData.splitlines()
        for line in partitionData:
            line = line.strip()
            if (not line or line[0] == '#'):
                continue
            self.partitionInfoList.append(self.parseFileToFlashEntry(line))
            l_numPartitions += 1

        self.log ("Total images in FileToFlash : " + str(l_numPartitions))
        os.chdir(cwd)
        return 0

    def startADBServer(self, sudo = ""):
        self.vlog("Starting ADB server ")
        adbCommand = sudo + " " + self.f_AdbTool + " start-server"
        result = self.shellUtils.executeShellCommand(adbCommand)
        if(result != nverror.NvError_Success):
            self.log("failed to start-server")
        return result

    def ListADBDevices(self):
        self.log("listing connected adb devices")
        adbListDevicesCommand = self.f_AdbTool + " devices -l"
        result = self.shellUtils.executeShellCommand (adbListDevicesCommand, True)

        self.log("listing connected adb devices with sudo")
        adbListDevicesCommand = self.f_AdbTool + " devices -l"
        result = self.shellUtils.executeShellCommand (adbListDevicesCommand, True)

        return result

    def AdbFindDevice(self, timeout_max = 100, sudo = ""):
        duration = 0
        totaltime = 0
        result = -1
        debug = False
        duration = 5

        self.log("Block until device is online, with a time out of " + str(timeout_max) + " seconds:")
        self.ListADBDevices()

        self.shellUtils.exceptlog = False
        adbCommand = sudo + "timeout " + str(duration) + " " + self.f_AdbTool + " -s " + self.targetConfig.s_AdbSerialNum + " wait-for-device"
        result = self.shellUtils.executeShellCommand(adbCommand, False, debug)

        while(totaltime < timeout_max and result != nverror.NvError_Success):
            self.ListADBDevices()
            result = self.shellUtils.executeShellCommand(adbCommand, False, debug)
            self.log("the ADB device is not yet found online : "  + self.targetConfig.s_AdbSerialNum)
            self.log("current totaltime in finding device online : " + str(totaltime) )
            totaltime += duration

        if(result != nverror.NvError_Success):
            self.log("failed to find the device :" + self.targetConfig.s_AdbSerialNum + " online at the end of :" + str(totaltime) + " seconds ")
        else:
            self.log("found  the device :" + self.targetConfig.s_AdbSerialNum + " online at the end of :" + str(totaltime) + " seconds ")
        self.shellUtils.exceptlog = True

        return result

    def RestartAdbServer(self, sudoRequired):
        sudo = ""
        if (sudoRequired == True):
            sudo = "sudo -E "

        self.log("Killing the ADB server...")
        adbCommand = sudo + self.f_AdbTool + " kill-server"
        self.log(self.shellUtils.executeShellCommand(adbCommand, True))
        time.sleep(2)
        self.log("Re-starting the ADB server after killing...")
        result = self.startADBServer(sudo)
        if (result != nverror.NvError_Success):
            self.vlog("Could not start ADB server, after killing ADB server")
            AbnormalTermination("${s_ERROR_ADB_TIMEOUT}", nverror.NvError_Timeout)

    def CheckUSBServiceInit(self, timeout_max = 100, sudoRequired = False):
        duration = 0
        cmd_out = 1
        result = -1
        search_device = True
        sudo = ""
        if(sudoRequired == True):
            sudo = "sudo -E "

        self.vlog("Waiting for USB device. This may take up to " + str(timeout_max) + " seconds...")
        while (duration <= timeout_max and cmd_out != 0):
            shellCommand = "lsusb -d " + self.targetConfig.s_Nvidia_USB_ID + ":"
            cmd_out = int(self.shellUtils.executeShellCommand(shellCommand))
            if(cmd_out == nverror.NvError_Success):
                break
            time.sleep(1)
            duration += 1

        if (duration >= timeout_max):
            self.vlog("Can't find USB device for ADB commnunication")
            AbnormalTermination("${s_ERROR_ADB_TIMEOUT} -- ADB can't find device with lsusb", nverror.NvError_Timeout)

        self.log("Show ADB version")
        adbCommand = sudo + self.f_AdbTool + " version"
        self.log(self.shellUtils.executeShellCommand(adbCommand, True))
        self.vlog("Starting and finding the ADB device : " + self.targetConfig.s_AdbSerialNum)

        self.log("Show Host version")
        unameCommand = "uname -a"
        self.shellUtils.executeShellCommand(unameCommand, True)
        lsbrelCommand = "lsb_release -a"
        self.shellUtils.executeShellCommand(lsbrelCommand, True)

        self.log("Parsing the udevrule for 0955")
        udevruleCommand = "ATTRS{idVendor}==\"0955\""
        path = "/etc/udev/rules.d/"
        self.shellUtils.grepInFolder(path, udevruleCommand, False)

        result = self.startADBServer(sudo)
        if (result != nverror.NvError_Success):
            self.vlog("Could not start ADB server")

        while (search_device == True):
            result = self.AdbFindDevice(timeout_max, sudo)
            if (result != nverror.NvError_Success):
                self.log("Could not find the {} device online".format(self.targetConfig.s_AdbSerialNum))
                if (not self.targetConfig.d_ProcessInfo["child"]):
                    search_device = False
                    self.RestartAdbServer(sudoRequired)
                    result = self.AdbFindDevice(timeout_max, sudo)
                    if (result != nverror.NvError_Success):
                        self.vlog("Could not find a device online, after killing and re-starting ADBserver")
                        AbnormalTermination("${s_ERROR_ADB_TIMEOUT}", nverror.NvError_Timeout)
                else:
                    self.targetConfig.d_ProcessInfo["ctpQueue"].put(False)
                    search_device = self.targetConfig.d_ProcessInfo["ptcQueue"].get()
            else:
                if (self.targetConfig.d_ProcessInfo["child"]):
                    self.targetConfig.d_ProcessInfo["ctpQueue"].put(True)
                    search_device = self.targetConfig.d_ProcessInfo["ptcQueue"].get()
                else:
                    search_device = False

    # Execute on target and returns output or error code
    def AdbShell(self, l_command, returnOutput=False, use_temp_file=False):
        if(not(l_command != None) and (len(l_command) != 0)):
            AbnormalTermination("Empty command to execute in AdbShell", nverror.NvError_Adb)

        if use_temp_file:
            # Create a temporary file
            with tempfile.NamedTemporaryFile(mode='w+') as temp_file:
                # Write content to the file
                temp_file.write(l_command)
                # Get the file name
                temp_file_name = temp_file.name
                temp_file.flush()
                self.AdbPush(temp_file_name,"/tmp/command")
            adbCommand = self.f_AdbTool + " -s " + self.targetConfig.s_AdbSerialNum + " shell /tmp/wr_sh.sh /bin/bash /tmp/command"
            adbOutput = self.shellUtils.executeShellCommand(adbCommand, True)
        else:
            adbCommand = self.f_AdbTool + " -s " + self.targetConfig.s_AdbSerialNum + " shell /tmp/wr_sh.sh " + l_command
            adbOutput = self.shellUtils.executeShellCommand(adbCommand, True)

        if(isinstance(adbOutput, int) or returnOutput):
            return adbOutput
        else:
            index = adbOutput.find('EXITCODE=')
            if index != -1 :
                adbOutput = adbOutput[index + len('EXITCODE='):]
                adbList=adbOutput.split()
                EXITCODE = int(adbList[0])
            else:
                self.vlog(adbOutput)
                EXITCODE = 0

            self.log("EXITCODE = " + str(EXITCODE))
            return EXITCODE

    def AdbPush(self, l_f_src, l_f_dest="/tmp"):
        if(not os.path.isfile(l_f_src)):
            AbnormalTermination("No valid file in AdbPush", nverror.NvError_FileNotFound)

        AdbStartTime = time.time()

        self.log("Push file via ADB to target destination")

        adbCommand = self.f_AdbTool + " -s " + self.targetConfig.s_AdbSerialNum + " push " + l_f_src + "  " + l_f_dest

        for retry in range(3):
            result = self.shellUtils.executeShellCommand(adbCommand)

            if(result == nverror.NvError_Success):
                break
            if (retry == 2):
                AbnormalTermination("Adb push failed -- " + adbCommand, nverror.NvError_Adb)
            else:
                self.vlog("[AdbPush] failed retrying...")

        AdbFinishTime = time.time()

        AdbPushTime = AdbFinishTime - AdbStartTime
        # self.vlog("[AdbPush] : time to push = " + str(AdbPushTime))

        return AdbPushTime

    def AdbPull(self, l_f_src, l_f_dest_dir="/tmp"):
        if(not os.path.isdir(l_f_dest_dir)):
            AbnormalTermination("No valid destination directory given to AdbPull", nverror.NvError_FileNotFound)

        self.vlog("Pull file via ADB to " + l_f_dest_dir)

        adbCommand = self.f_AdbTool + " -s " + self.targetConfig.s_AdbSerialNum + " pull " + l_f_src + "  " + l_f_dest_dir + "/"

        result = self.shellUtils.executeShellCommand(adbCommand)

        if(result != nverror.NvError_Success):
            AbnormalTermination("Adb pull failed -- " + adbCommand, nverror.NvError_Adb)

    def StartPartitiionWriterProcess(self, adbTaskQueue):
        while (True):
            command = adbTaskQueue.get()
            if (command == None):
                self.log("Exiting partition write process!!")
                break

            if (self.AdbShell(command) != nverror.NvError_Success):
                AbnormalTermination("Command failed: %s" % command)

    def PushDataChunkToDevice(self, Data, LocalCounter, LocalChunkFileName, TempDir, Size=None, SubChunk=""):
        LocalChunkFileName = os.path.join(TempDir, f"{LocalChunkFileName}_{LocalCounter}{SubChunk}")
        if Size:
            Data = self.duplicate_binary_data(Data, Size)
        with open(LocalChunkFileName, "wb") as file:
            file.write(Data)

        self.AdbPush(LocalChunkFileName)
        return 0

    def PushFileChunkToDevice(self, LocalFileName, LocalCounter, LocalMaxWriteChunk, LocalChunkFileName, TempDir):
        LocalMaxWriteChunk = 0xA00000 #Chunk size optimized for speed
        MaximumSingleWriteSize = 0x1400000 #split file 20MB or larger

        LocalChunkFileName = os.path.join(TempDir, LocalChunkFileName + "_" + str(LocalCounter))
        ddCommand = "dd if=" + LocalFileName
        ddCommand += " of=" + LocalChunkFileName
        ddCommand += " bs=" + str(LocalMaxWriteChunk)
        ddCommand += " skip=" + str(LocalCounter)
        ddCommand += " count=1"

        returnValue = self.shellUtils.executeShellCommand(ddCommand)
        if(returnValue != nverror.NvError_Success):
            self.vlog("DD COMMAND FAILED")
            AbnormalTermination("Could not dd chunk", nverror.NvError_SystemCommandFailed)

        LocalFileSize = os.stat(LocalChunkFileName).st_size
        if (LocalFileSize == LocalMaxWriteChunk):
            # Only only push non zero files to target
            returnValue = filecmp.cmp(LocalChunkFileName, os.path.join(TempDir, "zeroFillMaxChunkSize.bin"), shallow=False)
            if (returnValue == False):
                if(self.targetConfig.BOOTBURN_PYTEST == False):
                    self.AdbPush(LocalChunkFileName)
            else:
                os.remove(LocalChunkFileName)
        else:
            if(self.targetConfig.BOOTBURN_PYTEST == False):
                self.AdbPush(LocalChunkFileName)

        return 0

    def getBlockDeviceNameOnTarget(self, deviceName):
        blockDevice = None
        sysBlockListing = self.AdbShell("ls -l /sys/block", True)
        deviceName = os.path.basename(deviceName)
        deviceName = deviceName.split(':')[0]
        for line in sysBlockListing.splitlines():
            if(line.find(deviceName) != -1):
                blockDevice = line.split()
                blockDevice = blockDevice[-3]
                break
        return blockDevice

    def getBlockDeviceSize(self, block):
        blockSize = None
        procPartitions = self.AdbShell("cat /proc/partitions", True)
        for line in procPartitions.splitlines():
            if(line and line.find(block) != -1):
                blockSize = line.split()
                blockSize = blockSize[2]

    def checkMd5(self, partitionInfo):
        md5 = partitionInfo.md5
        offset = partitionInfo.Start
        filename = partitionInfo.FileName

        size = os.path.getsize(filename)

        ddcmd =  "/tmp/nvdd --device " + partitionInfo.LinuxPartitionName
        ddcmd += " --startoffset " + str(offset)
        ddcmd += " --partsize " + str(size) + " --md5sum " + str(md5) + " --printmd5sum"

        # occasionally the EXITCODE=0 gets dropped
        i = 0
        while i < 3:
            targetMd5 = self.AdbShell(ddcmd, True)
            if(isinstance(targetMd5, int) or targetMd5.find('EXITCODE=0') == -1):
                if (isinstance(targetMd5, str) and (targetMd5.find('EXITCODE') == -1)):
                    self.vlog("Missing EXITCODE retrying...")
                    self.vlog(str(targetMd5))
                    i += 1
                else:
                    break
            else:
                break

        if(isinstance(targetMd5, int) or targetMd5.find('EXITCODE=0') == -1):
            self.vlog("MD5 for partition " + partitionInfo.FileName + " does not match ... written image corrupted")
            self.vlog("expected MD5 sum = " + str(partitionInfo.md5))
            self.vlog("\n\033[01;31m" + str(targetMd5) + "\n\033[0m")
            return nverror.NvError_Bad_MD5

        targetMd5 = targetMd5.splitlines()[0]
        targetMd5 = targetMd5.split(':')[1]

        if(self.targetConfig.s_debugOutput):
            self.vlog("\033[01;32m target md5 = " + targetMd5 + " matches host side md5 = " + partitionInfo.md5 + "\033[0m")

        return nverror.NvError_Success

    def partitionStartToPartitionCount(self, partitionInfo):
        LocalPartition = self.getBlockDeviceNameOnTarget(partitionInfo.LinuxPartitionName)
        if(not LocalPartition):
            self.vlog("Device " + partitionInfo.LinuxPartitionName + " does not exist or doesn't have valid block device!")
            self.vlog("Partitions on target are:")
            self.vlog(self.AdbShell("ls -l /sys/block", True))
            AbnormalTermination("s_ERROR_DEVICE_NOT_FOUND", nverror.NvError_DeviceNotFound)
        LocalPartition = (
            os.path.dirname(partitionInfo.LinuxPartitionName) + "/" + LocalPartition
        )
        count = self.AdbShell(f" parted -m {LocalPartition} unit b print | awk -F: '$2 == \"{partitionInfo.Start}B\" {{print $1}}'", True, use_temp_file=True).splitlines()[0]
        return count

    def FixSecondaryGPT(self, partitionInfo, queue):
        self.vlog("Fixing secondary GPT {}...".format(partitionInfo.LinuxPartitionName))

        if (queue != None):
            queue.put(os.getpid())
        LocalPartition = self.getBlockDeviceNameOnTarget(partitionInfo.LinuxPartitionName)
        if(not LocalPartition):
            self.vlog("Device " + partitionInfo.LinuxPartitionName + " does not exist or doesn't have valid block device!")
            self.vlog("Partitions on target are:")
            self.vlog(self.AdbShell("ls -l /sys/block", True))
            AbnormalTermination("s_ERROR_DEVICE_NOT_FOUND", nverror.NvError_DeviceNotFound)
        LocalPartition = (
            os.path.dirname(partitionInfo.LinuxPartitionName) + "/" + LocalPartition
        )
        self.AdbShell(f"echo -e \"Fix\\nFix\" | parted ---pretend-input-tty {LocalPartition} print", use_temp_file=True)
        #max_size="$(((device_size - partition - expandedPartStart) / 4096 * 4096))"

        if self.expandedPart:
            device_size = int(self.AdbShell(f"blockdev --getsize64 {LocalPartition}", True, use_temp_file=True).splitlines()[0])
            partition = int(partitionInfo.Size)
            expandedPartStart = int(self.expandedPart.Start)
            max_size = int(((device_size - partition - expandedPartStart) / 4096) * 4096)
            if max_size > 0:
                expandedPartEnd = expandedPartStart + max_size - 1
                self.vlog(f"Expanded partition end: {expandedPartEnd}")
                self.AdbShell(f"parted -s {LocalPartition} resizepart {self.partitionStartToPartitionCount(self.expandedPart)} {expandedPartEnd}B")
                self.resizeFilesystem(self.expandedPart, queue, max_size)


    def CreateNvDDCommand(self, inputbin, length, devicename, startoffset):
        return f"/tmp/nvdd --inputbin={inputbin} --partsize {length} --device {devicename} --startoffset={startoffset}"

    def process_write_command(self, file_chunk_name, length, partitionInfo, FileWritten, WrittenData,
                            UpdatePartitions, n_PipeLineOps, pytest, adbTaskQueue):
        WriteCmd = self.CreateNvDDCommand(f"/tmp/{file_chunk_name}",
                                        length, partitionInfo.LinuxPartitionName,
                                        int(partitionInfo.Start) + FileWritten + WrittenData)

        if self.targetConfig.is_l4t and not UpdatePartitions:
            WriteCmd += " --l4t "

        # Pipeline Emmc Operations
        if n_PipeLineOps:
            WriteCmd += " -a"

        if not pytest:
            adbTaskQueue.put(WriteCmd)
            adbTaskQueue.put(f"rm /tmp/{file_chunk_name}")


    def SendSparseFile(self, partitionInfo, LocalChunkFileName, TempDir, UpdatePartitions, n_PipeLineOps, pytest, adbTaskQueue):
        def split_into_chunks(data, chunk_size):
            for i in range(0, len(data), chunk_size):
                yield data[i:i+chunk_size]

        sparse_file = open(partitionInfo.FileName, 'rb')
        header_bin = sparse_file.read(28)
        header = struct.unpack("<I4H4I", header_bin)
        FileWritten = 0

        magic = header[0]
        blk_sz = header[5]
        total_chunks = header[7]
        if magic != 0xED26FF3A:
            sparse_file.close()
            return False
        LocalPartition = self.getBlockDeviceNameOnTarget(partitionInfo.LinuxPartitionName)
        if LocalPartition:
            self.vlog("Device " + partitionInfo.LinuxPartitionName + " does not exist or doesn't have valid block device!")
            self.vlog("Partitions on target are:")
            self.vlog(self.AdbShell("ls -l /sys/block", True))
            if (self.AdbShell(f"blkdiscard -f /dev/{LocalPartition} -o {partitionInfo.Start} -l {partitionInfo.Size}") != nverror.NvError_Success):
                self.log(f"Device {LocalPartition} cannot be deleted fast. It is not recommended to use sparse file")

        for Counter in range(1, total_chunks + 1):
            data = None
            fill_bin = None
            header_bin = sparse_file.read(12)
            header = struct.unpack("<2H2I", header_bin)
            chunk_type = header[0]
            chunk_sz = header[2]
            total_sz = header[3]
            data_sz = total_sz - 12
            if chunk_type == 0xCAC1:
                # Raw data chunk
                if data_sz != (chunk_sz * blk_sz):
                    print(f"Raw chunk input size ({data_sz}) does not match output size ({chunk_sz * blk_sz})")
                    AbnormalTermination("Invalid Sparse File")
                else:
                    data = sparse_file.read(data_sz)
            elif chunk_type == 0xCAC2:
                # Fill data chuck
                if data_sz != 4:
                    print(f"Fill chunk should have 4 bytes of fill, but this has {data_sz}")
                    AbnormalTermination("Invalid Sparse File")
                else:
                    fill_bin = sparse_file.read(4)
            elif chunk_type == 0xCAC3:
                # Dont care chuck
                if data_sz != 0:
                    print(f"Don't care chunk input size is non-zero ({data_sz})")
                    AbnormalTermination("Invalid Sparse File")
            elif chunk_type == 0xCAC4:
                # CRC data chunk
                if data_sz != 4:
                    print(f"CRC32 chunk should have 4 bytes of CRC, but this has {data_sz}")
                    AbnormalTermination("Invalid Sparse File")
                else:
                    crc_bin = sparse_file.read(4)
                    crc = struct.unpack("<I", crc_bin)
                    print("Unverified CRC32 0x%08X" % (crc))
            else:
                print("Unknown chunk type 0x%04X" % (chunk_type))
                break
            if data:
                WrittenData = 0
                sub_chunk = 0
                for chunk in split_into_chunks(data, 0xA00000):
                    length = len(chunk)
                    file_chunk_name=f"{LocalChunkFileName}_{Counter}_{sub_chunk}"
                    file_chunk = os.path.join(TempDir,  file_chunk_name)
                    retVal = self.PushDataChunkToDevice(chunk, Counter, LocalChunkFileName, TempDir, SubChunk=f"_{sub_chunk}")
                    if (retVal != 0):
                        # FIXME: extract file name and mention file name in the log
                        AbnormalTermination("Unable to push file to target device")
                    if (os.path.isfile(file_chunk)):
                        self.process_write_command(file_chunk_name, length, partitionInfo, FileWritten,
                                                   WrittenData, UpdatePartitions,
                                                   n_PipeLineOps, pytest, adbTaskQueue)
                        os.remove(file_chunk)

                    sub_chunk += 1
                    WrittenData += length

            if fill_bin and fill_bin != b'\x00' * len(fill_bin):
                retVal = self.PushDataChunkToDevice(fill_bin, Counter, LocalChunkFileName, TempDir, Size=0xA00000)
                if (retVal != 0):
                    # FIXME: extract file name and mention file name in the log
                    AbnormalTermination("Unable to push file to target device")
                file_chunk = os.path.join(TempDir, LocalChunkFileName + "_" + str(Counter))
                if (os.path.isfile(file_chunk)):
                    TotalSize = chunk_sz * blk_sz
                    WrittenData = 0
                    while WrittenData <= TotalSize:
                        self.process_write_command(f"{LocalChunkFileName}_{Counter}",
                                                   min(0xA00000, TotalSize, TotalSize - WrittenData), partitionInfo,
                                                   FileWritten, WrittenData, UpdatePartitions, n_PipeLineOps,
                                                   pytest, adbTaskQueue)

                        WrittenData += min(0xA00000, TotalSize, TotalSize - WrittenData)
                    os.remove(file_chunk)

            FileWritten+=chunk_sz * blk_sz

        sparse_file.close()
        return True

    def SendFileUsingADB(self, partitionInfo, LocalSkipWriteZeroChunk, TempDir, queue, UpdatePartitions=[]):
        global localStartTime
        localStartTime = time.time()
        LocalMaxWriteChunk = 0xA00000 #Chunk size optimized for speed
        MaximumSingleWriteSize = 0x1400000 #split file 20MB or larger
        LocalChunkFileName = os.path.basename(partitionInfo.LinuxPartitionName)
        pytest = self.targetConfig.BOOTBURN_PYTEST
        md5Test = nverror.NvError_Success
        FileSize = os.stat(partitionInfo.FileName).st_size

        n_PipeLineOps = False
        # Pipeline Emmc Operations
        if (self.targetConfig.parsed_commandline["y"] == False):
            if (("sdhci" in str(partitionInfo.LinuxPartitionName)) or
                ("ufshci" in str(partitionInfo.LinuxPartitionName))):
                n_PipeLineOps = True

        if (queue != None):
            queue.put(os.getpid())
        self.vlog("Flashing Partition - " + partitionInfo.PartitionName + " of size " + partitionInfo.Size + " of File " + partitionInfo.FileName + " of size " + str(FileSize))

        if (FileSize == 0):
            self.vlog("Skip to flash " + partitionInfo.PartitionName + " as size of " + partitionInfo.FileName + " is zero")
            if ("ufshci" in str(partitionInfo.LinuxPartitionName)):
                self.vlog("Erasing Ufs %s Start %s Size %s\n" % (partitionInfo.PartitionName, str(partitionInfo.Start), str(partitionInfo.Size)))
                WriteCmd = "/tmp/nvdd --inputbin=/dev/zero"
                WriteCmd += " --device=" + str(partitionInfo.LinuxPartitionName)
                WriteCmd += " --startoffset=" + str(partitionInfo.Start)
                WriteCmd += " --partsize=" + str(partitionInfo.Size)
                if self.targetConfig.n_RBVerification == True:
                    WriteCmd += " --readback"
                if self.targetConfig.is_l4t and not UpdatePartitions:
                    WriteCmd += " --l4t"

                # Pipeline Emmc Operations
                if (n_PipeLineOps == True):
                    WriteCmd += " -a"

                if(pytest == False):
                    if (self.AdbShell(WriteCmd) != nverror.NvError_Success):
                        AbnormalTermination("Write failed on (%s)" % str(partitionInfo.LinuxPartitionName), nverror.NvError_FileWriteFailed)

            return nverror.NvError_Success

        if (FileSize > int(partitionInfo.Size)):
            self.vlog("partition size is smaller than the file size " + partitionInfo.PartitionName + " is size of " + partitionInfo.Size + "File "+ partitionInfo.FileName + " is size of " + str(FileSize))
            AbnormalTermination("Patition Size error in ADBPush, Parition size is less than File size", nverror.NvError_AdbPush)

        # If less than 10M directly transfer and write the binary
        if(FileSize <= MaximumSingleWriteSize):
            if(pytest == False):
                self.AdbPush(partitionInfo.FileName)
            WriteCmd = "/tmp/nvdd --inputbin=/tmp/" + os.path.basename(partitionInfo.FileName)
            WriteCmd += " --device=" + str(partitionInfo.LinuxPartitionName)
            WriteCmd += " --startoffset=" + str(partitionInfo.Start)
            WriteCmd += " --partsize=" + str(partitionInfo.Size)
            if self.targetConfig.n_RBVerification == True:
                WriteCmd += " --readback"
            if self.targetConfig.is_l4t and not UpdatePartitions:
                WriteCmd += " --l4t"

            # Pipeline Emmc Operations
            if (n_PipeLineOps == True):
                WriteCmd += " -a"

            if(pytest == False):
                if (self.AdbShell(WriteCmd) != nverror.NvError_Success):
                    AbnormalTermination("Write failed on (%s)" % str(partitionInfo.LinuxPartitionName), nverror.NvError_FileWriteFailed)
                self.AdbShell("rm /tmp/" + os.path.basename(partitionInfo.FileName))
                md5Test = self.checkMd5(partitionInfo)
        else:
            FileWritten = 0
            Counter = 0

            l_SplitPartSize = 0
            l_tracingEnabled = self.targetConfig.n_EnableTraceCall

            zero_file = os.path.join(TempDir, "zeroFillMaxChunkSize.bin")
            if not os.path.isfile(zero_file):
                self.log("Create bin of chunk size filled with zeros")
                with open(zero_file, 'wb') as f:
                    f.write(b'\x00' * LocalMaxWriteChunk)

            # Create adb task queue
            adbTaskQueue = Queue(20)

            # Start Partition writer process
            partitionWriterProcess = Process(target=self.StartPartitiionWriterProcess, args=(adbTaskQueue, ))
            partitionWriterProcess.start()

            if not self.SendSparseFile(partitionInfo, LocalChunkFileName, TempDir, UpdatePartitions, n_PipeLineOps, pytest, adbTaskQueue):
                retVal = self.PushFileChunkToDevice(partitionInfo.FileName, Counter, LocalMaxWriteChunk, LocalChunkFileName, TempDir)
                if (retVal != 0):
                    # FIXME: extract file name and mention file name in the log
                    AbnormalTermination("Unable to push file to target device")

                self.vlog("writing file " + partitionInfo.FileName)
                while FileWritten < FileSize:
                    self.vlog("FileWritten - " + str(FileWritten) + " :: FileSize - " + str(FileSize))
                    if (queue != None):
                        queue.put(os.getpid())
                    if (partitionWriterProcess.exitcode != None and partitionWriterProcess.exitcode != 0):
                        AbnormalTermination("Flashing Failed Propagated Error", partitionWriterProcess.exitcode)

                    if(FileWritten + LocalMaxWriteChunk <= FileSize):
                        retVal = self.PushFileChunkToDevice(partitionInfo.FileName, Counter + 1, LocalMaxWriteChunk, LocalChunkFileName, TempDir)
                        if (retVal != 0):
                            # FIXME: extract file name and mention file name in the log
                            AbnormalTermination("Unable to push file to target device")

                    # l_SplitPartSize is minimum of partition to be wriiten left and LocalMaxWriteChunk
                    if (LocalMaxWriteChunk < int(partitionInfo.Size) - FileWritten):
                        l_SplitPartSize = LocalMaxWriteChunk
                    else:
                        l_SplitPartSize = int(partitionInfo.Size) - FileWritten
                    self.vlog("l_SplitPartSize-" + str(l_SplitPartSize) + " : FileSize-" + str(FileSize) +": partitionInfo.Size-" + partitionInfo.Size + " : FileWritten-" + str(FileWritten) + " : BalanceToWrite-" + str(int(partitionInfo.Size) - FileWritten))

                    file_chunk = os.path.join(TempDir, LocalChunkFileName + "_" + str(Counter))
                    if (os.path.isfile(file_chunk)):
                        WriteCmd = "/tmp/nvdd --inputbin=/tmp/" + LocalChunkFileName + "_" + str(Counter)
                        WriteCmd += " --partsize " + str(l_SplitPartSize)
                        WriteCmd += " --device " + partitionInfo.LinuxPartitionName
                        WriteCmd += " --startoffset=" + str(int(partitionInfo.Start) + FileWritten)
                        if self.targetConfig.n_RBVerification == True:
                            WriteCmd += " --readback "
                        if self.targetConfig.is_l4t and not UpdatePartitions:
                            WriteCmd += " --l4t "

                        # Pipeline Emmc Operations
                        if (n_PipeLineOps == True):
                            WriteCmd += " -a"

                        if(pytest == False):
                            adbTaskQueue.put(WriteCmd)
                            adbTaskQueue.put("rm /tmp/" + LocalChunkFileName + "_" + str(Counter))
                        os.remove(file_chunk)
                    elif (LocalSkipWriteZeroChunk == False):
                        WriteCmd = "/tmp/nvdd --inputbin=/dev/zero --partsize " + str(l_SplitPartSize) + \
                                " --device " + partitionInfo.LinuxPartitionName + " --startoffset=" + str(int(partitionInfo.Start) + FileWritten)
                        if (self.targetConfig.n_RBVerification == True):
                            WriteCmd += " --readback "
                        if self.targetConfig.is_l4t and not UpdatePartitions:
                            WriteCmd += " --l4t "

                        # Pipeline Emmc Operations
                        if (n_PipeLineOps == True):
                            WriteCmd += " -a"

                        if(pytest == False):
                            adbTaskQueue.put(WriteCmd)

                    FileWritten = FileWritten + LocalMaxWriteChunk
                    Counter += 1
                    # if (FileWritten <= FileSize):
                    #    self.vlog("{}% done".format(round(FileWritten * 100 / FileSize, 3)))
                    # else:
                    #    self.vlog("100% done")

                    # turn off tracing after 1 iteration to avoid long trace files
                    if (self.targetConfig.n_EnableTraceCall == True):
                        self.vlog("Turning off tracing after 1 iteration (to avoid long trace file)")
                        self.targetConfig.n_EnableTraceCall = False

            # Writer process exit message
            adbTaskQueue.put(None)
            partitionWriterProcess.join()
            if (partitionWriterProcess.exitcode != 0):
                AbnormalTermination("Flashing Failed Propagated Error ", partitionWriterProcess.exitcode)

            if ((self.targetConfig.parsed_commandline["safety"]) or
                (("n_md5SumAll" in self.targetConfig.boardDefaultPaths) and
                 (self.targetConfig.boardDefaultPaths["n_md5SumAll"] == str(True)))):
                md5Test = self.checkMd5(partitionInfo)

            # restore tracing setting
            if (l_tracingEnabled == True):
                self.targetConfig.n_EnableTraceCall = True
                self.log("Restoring tracing after {}} iterations".format(Counter))

        if(md5Test != nverror.NvError_Success):
            self.vlog("Target image written incorrectly ... corrupted")
            return md5Test
        else:
            self.log("Image written correctly, md5 sum matches")

        # Resize file system to device size, we can encounter two cases:
        # 1) Filesystem is at the beginning of the the partitionInfo.LinuxPartitionName:
        #    We can just run resize2fs on the partition to resize the fs on the partition
        #    assuming that the partition just contains filesystem.
        # 2) Filesystem is placed at non-zero offset in partitionInfo.LinuxPartitionName:
        #    We need to attach part of the partitionInfo.LinuxPartitionName containing filesystem area to a loop
        #    device(using losetup) and then resize the fs on the loop device.

        if ((int(partitionInfo.Resize) >= 1) and (FileSize < int(partitionInfo.Size))):
            self.resizeFilesystem(partitionInfo, queue)
            if int(partitionInfo.Resize) == 2:
                self.expandedPart = partitionInfo

        return nverror.NvError_Success

    def resizeFilesystem(self, partitionInfo, queue, size=None):
        """Resizes the filesystem on the given partition to fill available space"""
        self.vlog("Resizing filesystem on {}...".format(partitionInfo.LinuxPartitionName))

        if (queue != None):
            queue.put(os.getpid())
        if not size:
            size = partitionInfo.Size
        LocalPartition = self.getBlockDeviceNameOnTarget(partitionInfo.LinuxPartitionName)
        if(not LocalPartition):
            self.vlog("Device " + partitionInfo.LinuxPartitionName + " does not exist or doesn't have valid block device!")
            self.vlog("Partitions on target are:")
            self.vlog(self.AdbShell("ls -l /sys/block", True))
            AbnormalTermination("s_ERROR_DEVICE_NOT_FOUND", nverror.NvError_DeviceNotFound)

        LocalPartition = os.path.dirname(partitionInfo.LinuxPartitionName) + "/" + LocalPartition
        LoopDevice = None

        if (int(partitionInfo.Start) != 0):
            LoopDevice = "/dev/block/loop0"
            if os.path.getsize(os.path.join(self.targetConfig.p_fsUtils, "losetup")) == 0:
                abs_path=""
            else:
                abs_path="/tmp/"

            self.log("Using {} to resize FS on {} ...".format(LoopDevice, partitionInfo.LinuxPartitionName))  #
            self.AdbPush(os.path.join(self.targetConfig.p_fsUtils, "losetup"))

            adbOutput = self.AdbShell("umount " + LoopDevice)
            if(adbOutput > 2 and adbOutput != 32):
                AbnormalTermination ("s_ERROR_ADB_TARGET_SCRIPT", nverror.NvError_SystemCommand)

            adbOutput = self.AdbShell(abs_path + "losetup -d " + LoopDevice)
            if(adbOutput > 2):
                AbnormalTermination ("s_ERROR_ADB_TARGET_SCRIPT", nverror.NvError_SystemCommand)

            adbOutput = self.AdbShell(abs_path + "losetup " + LoopDevice + " " + LocalPartition + " --offset " + partitionInfo.Start + " --sizelimit " + str(size))
            if(adbOutput > 2):
                AbnormalTermination ("s_ERROR_ADB_TARGET_SCRIPT", nverror.NvError_SystemCommand)

            LocalPartition = LoopDevice
            self.vlog("done")

        self.AdbPush(os.path.join(self.targetConfig.p_fsUtils, "e2fsck"))

        adbOutput = self.AdbShell(abs_path + "e2fsck -yfFt " + LocalPartition)
        # if(adbOutput > 2):
        #    AbnormalTermination ("s_ERROR_ADB_TARGET_SCRIPT", nverror.NvError_SystemCommand)

        self.AdbPush(os.path.join(self.targetConfig.p_fsUtils, "resize2fs"))

        self.AdbShell(abs_path + "resize2fs -fFp " + LocalPartition)
        self.AdbShell("sync")
        if LoopDevice:
            self.AdbShell(abs_path + "losetup -d " + LoopDevice)

        self.vlog("done")

        return nverror.NvError_Success

    def duplicate_binary_data(self, data, target_size):
        duplicated = data * (target_size // len(data))
        remainder = target_size % len(data)
        if remainder:
            duplicated += data[:remainder]
        return duplicated

    # Erase the specified partitions listed in ErasePartitions
    def EraseUsingADB(self, l_FileName, ErasePartitions, TempDir):
        self.vlog("Erasing partitions using ADB ****")

        if (not(ErasePartitions and len(ErasePartitions) > 0) ):
            self.vlog("No partitions to erase, returning from EraseUsingADB")
            return
        l_numErasePartitions = 0

        cwd = os.getcwd()
        print ("TempDir :: " + TempDir)
        os.chdir(os.path.join(TempDir))

        with open(l_FileName, 'r') as fileToFlash:
            partitionData = fileToFlash.read()

        partitionData = partitionData.splitlines()
        for line in partitionData:
            line = line.strip()
            if (not line or line[0] == '#'):
                continue
            partitionInfo = self.parseFileToFlashEntry(line)
            if (partitionInfo.PartitionName not in ErasePartitions):
                continue
            else:
                l_numErasePartitions += 1

            if (self.AdbShell("/tmp/nvdd --device " + partitionInfo.LinuxPartitionName + " -E eoffset=" + str(partitionInfo.Start) + ",esize=" + str(partitionInfo.Size)) != 0):
                self.log("Erasing partition(%s) of device (%s) Failed" % (partitionInfo.Name,partitionInfo.LinuxPartitionName))
                AbnormalTermination("Erasing partition(%s) Failed" % (partitionInfo.Name), nverror.NvError_BadParameter)
        if (l_numErasePartitions != len(ErasePartitions)):
            self.log("Erasing partitions  Failed" )
            AbnormalTermination("Erased partitions (%s) are not equal to size of input Partition List - %s " % (l_numErasePartitions, len(ErasePartitions)), nverror.NvError_BadParameter)
        os.chdir(cwd)
        return 0

    def validateBch(self, newBch, oldFileName):
        # TODO: check if the new BCH matches with old BCH
        # by comparing SHA of the binary in both the BCHs
        nvBchValidateCmd = self.flashUtils.f_NvBchValidate + " --chip " + str(self.targetConfig.GetChipID()) + \
                           " --base_image " + oldFileName + " --new_image " + newBch

        self.log("Executing command: %s" % nvBchValidateCmd)

        try:
            self.shellUtils.executeShellCommand(nvBchValidateCmd, False, True)
        except OSError as err:
            if (err.errno == 0x25):
                return False

            raise err

        # If there is no exception return valid
        return True

    def updateBch(self, newBch, oldFileName):
        tf = tempfile.NamedTemporaryFile(delete=False)
        with open(tf.name, 'wb') as tmpFile:
            # Write new BCH into temp file
            with open(newBch, "rb") as newBchFile:
                tmpFile.write(newBchFile.read())

            # Write binary into temp file
            with open(oldFileName, "rb") as fileWithOldBch:
                fileWithOldBch.seek(8192, 0)
                tmpFile.write(fileWithOldBch.read())
        return tf

    def dup_pt_to_fill_partition(self, partitionInfo):
        tf = tempfile.NamedTemporaryFile(delete=False, suffix='_pt')
        pt_data = None
        with open(partitionInfo.FileName, "rb") as pt_file:
            pt_data = pt_file.read()
        cur_pt_sz = len(pt_data)
        num_cpy = int(partitionInfo.Size)//cur_pt_sz

        with open(tf.name, "wb") as tempFile:
            for i in range(0, num_cpy):
                tempFile.write(pt_data)
        return tf

    # Arg1 contains the list of partitions to be updated
    def FlashUsingADB(self, l_FileName, UpdatePartitions, TempDir, queue):
        SkipWriteZeroChunk = 0
        n_SkipFileSystemParts = self.targetConfig.parsed_commandline["s"]
        n_SkipFlashingRecoveryParts = self.targetConfig.parsed_commandline["o"]
        n_FastFlash = self.targetConfig.parsed_commandline["O"]
        if (n_FastFlash):
            n_SkipFlashingRecoveryParts = n_FastFlash
        n_FlashMods = self.targetConfig.parsed_commandline["m"]
        l_numUpdatePartsPresent = 0

        l_numUpdatePartsFound = 0
        l_BctPartBin = self.targetConfig.dieId + "_bctCopiesBlob.tmp"
        l_BadPagePartBin = self.targetConfig.dieId + "_badPageBlob.tmp"

        if(self.targetConfig.BOOTBURN_PYTEST):
            n_SkipFileSystemParts = True

        if (self.targetConfig.n_FlashHypervisor and (len(UpdatePartitions) == 0) and (n_SkipFileSystemParts == False)):
            self.EraseEmmc(l_FileName, TempDir)
            if (self.targetConfig.parsed_commandline["clean"]):
                self.EraseQspi(l_FileName, TempDir)

        elif (self.targetConfig.is_l4t) and (len(UpdatePartitions) == 0):
            self.EraseQspi(l_FileName, TempDir)
            if (self.targetConfig.parsed_commandline["clean"]):
                self.EraseExternalStorage(l_FileName)

        cwd = os.getcwd()
        os.chdir(os.path.join(TempDir))

        # read lines from fd 7 to which FileToFlash.txt is redirected to
        fileData = self.shellUtils.catFile(l_FileName)
        fileData = fileData.splitlines()
        for line in fileData:
            line = line.strip()
            if(not line or line[0] == '#'):
                continue

            if (queue != None):
                queue.put(os.getpid())
            partitionInfo = self.parseFileToFlashEntry(line)
            deviceName = os.path.basename(partitionInfo.LinuxPartitionName)
            PartitionBinary = partitionInfo.FileName

            l_numUpdatePartsPresent += 1

            if(UpdatePartitions):
                if ((partitionInfo.PartitionName in UpdatePartitions) == False):
                    continue
                else:
                    l_numUpdatePartsFound += 1

            l_NotSpecialFastFlash = True
            if (n_SkipFlashingRecoveryParts):
                if (n_FastFlash) and  (partitionInfo.PartitionName == 'B_mb1-bootloader'):
                    l_NotSpecialFastFlash = False
                    BinSize = os.stat(partitionInfo.FileName).st_size
                    # Munge name to perserve file
                    partitionInfo.FileName = partitionInfo.FileName + '-ff'
                    dd_cmd = "dd if=/dev/zero of=" + partitionInfo.FileName + " ibs=1 count=" + str(BinSize)
                    result = self.shellUtils.executeShellCommand(dd_cmd)
                    if (result != nverror.NvError_Success):
                        AbnormalTermination("s_ERROR_INVALID_PARAMS -- dd failed", nverror.NvError_InvalidArgument)
                    partitionInfo.md5 = hashlib.md5(open(partitionInfo.FileName,'rb').read()).hexdigest()

                if (l_NotSpecialFastFlash):
                    chain = tuple(["B_", "C_", "D_"])
                    if (partitionInfo.PartitionName.endswith("-r") or partitionInfo.PartitionName.startswith(chain)):
                        self.vlog("Skipping flashing recovery partition " + partitionInfo.PartitionName)
                        continue

            if (self.targetConfig.n_FlashLinux and n_FlashMods == False):
                if (partitionInfo.PartitionName.find("ist") != -1):
                    self.vlog("Skipping flashing IST partition " + partitionInfo.PartitionName)
                    continue

            tf = None
            filePath, _ = os.path.splitext(os.path.basename(partitionInfo.FileName))
            if (self.targetConfig.headers and
                partitionInfo.PartitionName != 'bct' and
                partitionInfo.PartitionName != 'pt' ):
                # Check if the headers are matching using SHA validation
                filePath = os.path.join(self.targetConfig.headers, filePath + "_resigned.bch")
                if (os.path.exists(filePath)):
                    if (self.validateBch(filePath, partitionInfo.FileName)):
                        # Replace the BCH with the corresponding header
                        tmpFile = self.updateBch(filePath, partitionInfo.FileName)

                        partitionInfo.FileName = tmpFile.name
                        partitionInfo.md5 = hashlib.md5(open(partitionInfo.FileName ,'rb').read()).hexdigest()
                        self.log("File name with updated BCH: %s" % partitionInfo.FileName)
                    else:
                        AbnormalTermination("Invalid BCH header found: %s" % partitionInfo.FileName)

            if(self.targetConfig.headers and 'A_fskp-fw' in partitionInfo.PartitionName):
                # Check if the headers has fskp fw, fail otherwise
                filePath = os.path.join(self.targetConfig.headers, self.targetConfig.fskpBinName)
                if (os.path.exists(filePath)):
                    partitionInfo.FileName = filePath
                    with open(partitionInfo.FileName ,'rb') as fskp_file:
                        partitionInfo.md5 = hashlib.md5(fskp_file.read()).hexdigest()
                    self.log("Using fskp fw at: %s" % filePath)
                else:
                    msg = ("FSKP fw not found at: %s" % filePath)
                    AbnormalTermination(msg, nverror.NvError_FileNotFound)

            if partitionInfo.PartitionName == "pt" :
                if self.targetConfig.headers :
                    filePath, _ = os.path.splitext(os.path.basename(partitionInfo.FileName))
                    partitionInfo.FileName = os.path.join(self.targetConfig.headers, filePath + "_resigned.bin")
                tmpFile = self.dup_pt_to_fill_partition(partitionInfo)
                partitionInfo.FileName = tmpFile.name
                partitionInfo.md5 = hashlib.md5(open(partitionInfo.FileName ,'rb').read()).hexdigest()
                self.log("File name with expanded PT: %s" % partitionInfo.FileName)

            self.vlog("["+ deviceName + "] Flashing for Partition " + partitionInfo.PartitionName)
            bct_pattern = r'^[A-Z]_bct'
            if (re.match(bct_pattern, partitionInfo.PartitionName)):
                # This is chain specific BR BCT partition, legacy BR BCT replication
                # is not required for chain specific partitions
                # Just update the md5 sum because content might get changed when
                # preserving SKU info/customer data
                partitionInfo.md5 = hashlib.md5(open(partitionInfo.FileName,'rb').read()).hexdigest()
            if (partitionInfo.PartitionName == "bct"):
                # Replicate BCT in multiple copies
                BCTfileSize = os.stat(partitionInfo.FileName).st_size
                if (self.targetConfig.n_Asymmetric):
                    asymmetric = AsymmetricFlashing(self.targetConfig.brBctLayoutJsonFile)

                    if (self.targetConfig.headers):
                        print("New path %s\n" % self.targetConfig.headers)
                        newfskpBrBct = os.path.join(self.targetConfig.headers, self.targetConfig.fskpBrBct)

                        if (not os.path.exists(newfskpBrBct)):
                            msg = ("FSKP Br Bct not found at: %s" % newfskpBrBct)
                            AbnormalTermination(msg, nverror.NvError_FileNotFound)

                        asymmetric.merge_bcts_into_blob(l_BctPartBin, self.targetConfig.GetChipID(), newfskpBrBct, self.flashUtils.f_NvSkuInfo)
                    else:
                        print("old school \n")
                        asymmetric.merge_bcts_into_blob(l_BctPartBin)
                elif (BCTfileSize < 16384):
                    # Check if new BCT is present in headers
                    if (self.targetConfig.headers):
                        filePath = os.path.join(self.targetConfig.headers, filePath + "_resigned.bct")
                        if (os.path.exists(filePath)):
                            partitionInfo.FileName = filePath
                            partitionInfo.md5 = hashlib.md5(open(partitionInfo.FileName ,'rb').read()).hexdigest()
                            self.log("Updated BCT file: %s" % partitionInfo.FileName)
                        else:
                            AbnormalTermination("BCT not found in headers directory: %s" % partitionInfo.FileName)

                    self.log("Remove previous instances of BCT")
                    self.shellUtils.removeFile(os.path.join(TempDir, l_BctPartBin))
                    self.log("Copy partition BCT to destination")
                    self.shellUtils.Copy(partitionInfo.FileName, os.path.join(TempDir, l_BctPartBin))
                    # For L4T, make BCT size equal to 256KiB. BCT partition should at least contains three BCT copies.
                    # Otherwise, make BCT size equal to 16KiB.
                    if (self.targetConfig.is_l4t):
                        bctBlockSize = 262144
                        if (bctBlockSize * 3 > int(partitionInfo.Size)):
                            AbnormalTermination("BCT partition size is smaller than %s" % str(bctBlockSize * 3))
                    else:
                        bctBlockSize = 16384

                    self.log("Make BCT size equal to " + str(bctBlockSize // 1024) + "KB by padding zeros at the end")

                    ddCommand = "dd if=/dev/zero of=" + l_BctPartBin + " seek=" + str(BCTfileSize) + " count=" + str(bctBlockSize - BCTfileSize) + " bs=1"
                    self.shellUtils.executeShellCommand(ddCommand)
                    self.log("Copy resulting file to destination")

                    tmpBctPath = os.path.join(TempDir, self.targetConfig.dieId + "_bct.tmp")
                    self.shellUtils.Copy(l_BctPartBin, tmpBctPath)

                    # Create multiple copy of BCT for size of the partition
                    self.log("Replicate BCT and append to destination. Repeated for size of the partition")

                    # For L4T, write BCT image to block 0 to block 2.
                    # Otherwise, write BCT image to the end of the BCT partition.
                    if (self.targetConfig.is_l4t):
                        end = bctBlockSize * 3
                    else:
                        end = int(partitionInfo.Size)
                    fid = open(tmpBctPath, 'rb')
                    bctData = fid.read()
                    fid.close()

                    start = 0
                    fid = open(l_BctPartBin, 'wb')
                    # For L4T when chain B is selected as default boot chain, skip block 0
                    # by writing "0xff" to it.
                    if (self.targetConfig.is_l4t) and (self.targetConfig.s_L4TBootChainSelect == "B"):
                        fid.write(bytes([0xff] * bctBlockSize))
                        start += bctBlockSize

                    while (start < end):
                        fid.write(bctData)
                        start += bctBlockSize
                    fid.close()

                    self.shellUtils.removeFile(tmpBctPath)
                else:
                    self.vlog("Invalid BCT size " + str(BCTfileSize))
                    AbnormalTermination("s_ERROR_TARGET_CONFIGURE", nverror.NvError_InvalidSize)

                partitionInfo.FileName = l_BctPartBin
                partitionInfo.md5 = hashlib.md5(open(l_BctPartBin,'rb').read()).hexdigest()

            if (partitionInfo.PartitionName == "bad-page"):
                # Replicate bad-page in two copies
                BadPagefileSize = os.stat(PartitionBinary).st_size

                if (BadPagefileSize < 262144):
                    # Make bad page 256K size
                    self.log("Remove previous instances of bad page")
                    self.shellUtils.removeFile(os.path.join(TempDir, l_BadPagePartBin))
                    self.log("Copy partition bad-page to destination")
                    self.shellUtils.Copy(PartitionBinary, os.path.join(TempDir, l_BadPagePartBin))

                    self.log("Make bad-page size equal to 256K by padding zeros at the end")
                    ddCommand = "dd if=/dev/zero of=" + os.path.join(TempDir, l_BadPagePartBin)
                    ddCommand += " seek=" + str(BadPagefileSize) + " count=" + str(262144 - BadPagefileSize) + " bs=1"
                    result = self.shellUtils.executeShellCommand(ddCommand)
                    if (result != nverror.NvError_Success):
                        self.vlog("Unable to append zero to bad page bin")
                        AbnormalTermination("s_ERROR_BOOTBURN_INTERNAL", nverror.NvError_SystemCommand)

                    self.log("Copy resulting file to destination")
                    self.shellUtils.Copy(os.path.join(TempDir, l_BadPagePartBin), os.path.join(TempDir, "bad-page.tmp"))

                else:
                    self.vlog("Invalid bad page size " + str(BadPagefileSize))
                    AbnormalTermination("s_ERROR_INVALID_PARAMS", nverror.NvError_SystemCommand)

                # Create multiple copies of bad-page for size of the partition,
                # As size of bad page parition is now 512K, we have now 2 copies of bad page
                self.vlog("Replicate bad-page and append to destination. Repeated for size of the partition")
                start = 262144
                fid = open(os.path.join(TempDir, "bad-page.tmp"), 'rb')
                badPageData = fid.read()
                fid.close()

                fid = open(os.path.join(TempDir, l_BadPagePartBin), 'ab')

                while (start < int(partitionInfo.Size)):
                    fid.write(badPageData)
                    start += 262144

                fid.close()

                self.shellUtils.removeFile(os.path.join(TempDir, "bad-page.tmp"))
                partitionInfo.FileName = os.path.join(TempDir, l_BadPagePartBin)
                partitionInfo.md5 = hashlib.md5(open(partitionInfo.FileName,'rb').read()).hexdigest()

            SkipWriteZeroChunk=0
            for l_device_sdhci in self.targetConfig.s_EmmcErasedArray:
                if (deviceName in l_device_sdhci):
                    SkipWriteZeroChunk=1
                    break

            result = self.SendFileUsingADB(partitionInfo, SkipWriteZeroChunk, TempDir, queue, UpdatePartitions)
            if(result != nverror.NvError_Success):
                self.vlog("Failed to correctly write partition " + partitionInfo.PartitionName)
                AbnormalTermination("Bad MD5 Sum on " + partitionInfo.PartitionName, nverror.NvError_Bad_MD5)
            is_qspi_partition = any([x in str(partitionInfo.LinuxPartitionName) for x in ["spi", "mtd"]])
            if (partitionInfo.PartitionName == "secondary_gpt" and not is_qspi_partition):
                self.FixSecondaryGPT(partitionInfo, queue)

        if((UpdatePartitions and len(UpdatePartitions) > 0) and (l_numUpdatePartsFound != len(UpdatePartitions))):
            self.vlog(str(UpdatePartitions) + " " + str(len(UpdatePartitions)) + " " + str(l_numUpdatePartsFound) + " " + str(l_numUpdatePartsPresent))
            self.vlog("Unable to find one or more partitions " + str(UpdatePartitions) + " in config file for which image is generated during flashing")
            AbnormalTermination("s_ERROR_PARSE_CONFIG", nverror.NvError_ConfigVarNotFound)

        os.chdir(cwd)
        if (tf):
            tf.delete()
        return 0

    def PushWraperShell(self):
        self.AdbPush(self.targetConfig.f_WrapperShellScrip)
        self.AdbPush(self.flashUtils.f_NvDDTool)
        self.AdbShell("chmod 766 /tmp/wr_sh.sh")
        self.AdbShell("chmod 766 /tmp/nvdd")
        if(self.targetConfig.parsed_commandline["m"]):
            self.log("identified MODS command, skipping  optin_fuse execution  on target")

    def ExecOptinFuse(self):
        self.AdbPush(self.flashUtils.f_NvOptinFuse)
        ChmodOptinCmd = "chmod 766 /tmp/" + os.path.basename(self.flashUtils.f_NvOptinFuse)
        ExecOptinCmd = "/tmp/" + os.path.basename(self.flashUtils.f_NvOptinFuse)
        if(nverror.NvError_Success != self.AdbShell(ChmodOptinCmd)):
            AbnormalTermination("Fail to update optin_fuse mode " , nverror.NvError_ExecOptinFuseFail)
        if(nverror.NvError_Success != self.AdbShell(ExecOptinCmd)):
            AbnormalTermination("Fail to execute optin_fuse binary " , nverror.NvError_ExecOptinFuseFail)

    def EraseEmmc(self, l_FileName, TempDir):
        l_sdhci_device=""

        cwd = os.getcwd()

        l_rpmb = self.shellUtils.grep(l_FileName, "/dev/block/mmcblk0rpmb")
        b_rpmb = ((l_rpmb != None) and (len(l_rpmb) != 0))
        for l_sdhci_device in self.s_sdmmc_device_name_list:

            l_sdhci = self.shellUtils.grep(l_FileName, l_sdhci_device)
            if (((l_sdhci != None) and (len(l_sdhci) != 0)) or
                  b_rpmb == True):
                l_erase_start = -1
                l_erase_end = 0
                l_erase_size = 0
                fileData = open(l_FileName, "r")
                os.chdir(TempDir)
                EraseStartTime=time.time()
                if (self.targetConfig.parsed_commandline["clean"] or self.targetConfig.n_Asymmetric):
                    l_erase_start = 0
                    self.vlog("Erasing eMMC(%s) .. Start %d Size %d" % (l_sdhci_device, l_erase_start, l_erase_size))
                    if (self.AdbShell("/tmp/nvdd --device " + l_sdhci_device + " -E eoffset=" + str(l_erase_start)+ ",esize=" + str(l_erase_size)) != 0):
                        self.log("Erasing Emmc(%s) Failed" % (l_sdhci_device))
                        AbnormalTermination("Emmc Erase(%s) Failed" % (l_sdhci_device), nverror.NvError_BadParameter)

                    self.targetConfig.s_EmmcErasedArray.append(l_sdhci_device)
                else:
                    for line in fileData:
                        if (line[0] == "#"):
                            continue

                        line=line.split()

                        device=line[0]
                        Start=int(line[3])
                        Size=int(line[4])

                        if (device == l_sdhci_device):
                            if (l_erase_start == -1):
                                l_erase_start=Start
                                l_erase_end=Start

                            if (l_erase_end == Start):
                                l_erase_size=l_erase_size + Size
                            else:
                                self.vlog("Erasing eMMC(%s) .. Start %d Size %d" % (l_sdhci_device, l_erase_start, l_erase_size))
                                if (self.AdbShell("/tmp/nvdd --device " + l_sdhci_device + " -E eoffset=" + str(l_erase_start) + ",esize=" + str(l_erase_size)) != 0):
                                    self.log("Erasing Emmc(%s) Failed" % (l_sdhci_device))
                                    AbnormalTermination("Emmc Erase(%s) Failed" % (l_sdhci_device), nverror.NvError_BadParameter)

                                l_erase_start=Start
                                l_erase_size=Size

                            l_erase_end=Start + Size
                    fileData.close()

                    if (l_erase_start != -1):
                        self.vlog("Erasing eMMC(%s) .. Start %d Size %d" % (l_sdhci_device, l_erase_start, l_erase_size))
                        if (self.AdbShell("/tmp/nvdd --device " + l_sdhci_device + " -E eoffset=" + str(l_erase_start)+ ",esize=" + str(l_erase_size)) != 0):
                            self.log("Erasing Emmc(%s) Failed" % (l_sdhci_device))
                            AbnormalTermination("Emmc Erase(%s) Failed" % (l_sdhci_device), nverror.NvError_BadParameter)

                        self.targetConfig.s_EmmcErasedArray.append(l_sdhci_device)

                EraseFinishTime=time.time()
                totalTime = EraseFinishTime-EraseStartTime
                self.vlog("Erase eMMC took " + str(totalTime))
                os.chdir(cwd)

    def EraseQspi(self, l_FileName, TempDir):
        l_qspi_device=""

        cwd = os.getcwd()

        for l_qspi_device in self.s_qspi_device_name_list:

            l_qspi = self.shellUtils.grep(l_FileName, l_qspi_device)
            if ((l_qspi != None) and (len(l_qspi) != 0)):
                l_erase_start = 0
                l_erase_size = 0
                EraseStartTime=time.time()
                self.vlog("Erasing Qspi(%s) .. Start %d Size %d" % (l_qspi_device, l_erase_start, l_erase_size))
                if (self.AdbShell("/tmp/nvdd --device " + l_qspi_device + " -E eoffset=" + str(l_erase_start)+ ",esize=" + str(l_erase_size)) != 0):
                    self.log("Erasing Qspi(%s) Failed" % (l_qspi_device))
                    AbnormalTermination("Qspi Erase (%s) Failed" % (l_qspi_device), nverror.NvError_BadParameter)

                EraseFinishTime=time.time()
                totalTime = EraseFinishTime-EraseStartTime
                self.vlog("Erase Qspi took " + str(totalTime))
                os.chdir(cwd)

    def EraseExternalStorage(self, l_FileName):
        """
        Find and erase NVMe, UFS, and other external storage devices.

        Args: l_FileName (str): FileToFlash.txt file name with partition information to be erased.
        """
        self.generatePartitionEntryData(l_FileName, os.path.join(self.targetConfig.p_OutDirPath, "flash-images"))
        if not self.partitionInfoList:
            return

        erase_storage_list = set()
        linux_partition_name_set = set()
        for partitionInfo in self.partitionInfoList:
            linux_partition_name_set.add(partitionInfo.LinuxPartitionName)
        for name in linux_partition_name_set:
            linux_device_name = f"/dev/{self.getBlockDeviceNameOnTarget(name)}"
            self.vlog(f"Translated LinuxPartitionName: {name} to BlockDeviceName: {linux_device_name}")
            for storage_type, device_name_re in self.s_external_storage_device_name_re.items():
                if not re.match(device_name_re, linux_device_name):
                    continue

                erase_storage_list.add(f"{storage_type} - {linux_device_name}")

        if not erase_storage_list:
            return

        self.log(f"Found external storage device(s) to erase: {', '.join(erase_storage_list)}")
        for erase_storage in erase_storage_list:
            erase_start_time = time.time()
            storage_type, linux_device_name = erase_storage.split(" - ")

            # blkdiscard with options forced (-f) and verbose (-v).
            self.vlog(f"Erasing {storage_type} ({linux_device_name})...")
            if (self.AdbShell(f"blkdiscard -f -v {linux_device_name}") != 0):
                self.log(f"Erasing {storage_type} ({linux_device_name}) Failed.")
                AbnormalTermination(f"{storage_type} Erase ({linux_device_name}) Failed", nverror.NvError_BadParameter)

            erase_finish_time = time.time()
            total_time = round(erase_finish_time - erase_start_time, 2)
            self.vlog(f"{storage_type} ({linux_device_name}) erase took {total_time} seconds.")

    # get a partition data from the target if available in the FileToFlash
    # which is generated with generatePartitionEntryData
    # this is used to get aT partition data from the target
    def getTargetPartitionData(self, partName, outFile, partSize=""):
        # parseFileToFlash.txt to retrieve the PVIT Entry
        partNameFound = False
        cwd = os.getcwd()
        for partInfo in self.partitionInfoList:
            if (partName == partInfo.PartitionName):
                if (len(partSize) != 0):
                    size = partSize
                else:
                    size = partInfo.Size
                ReadCmd = "/tmp/nvdd --device  " + partInfo.LinuxPartitionName + "  --partsize " + size + " --startoffset " + partInfo.Start + " --outputbin /tmp/" + outFile
                self.log("ReadCmd :: " + str(ReadCmd)  + "for partition - " + partName)
                self.AdbShell(ReadCmd)
                self.AdbPull("/tmp/" + outFile, cwd)
                partNameFound = True
                break
        return partNameFound

    def parsePartTableEntry(self, ptEntry):
        ptEntry = ptEntry.rstrip()
        ptEntry = ptEntry.split(' ')

        pt = parttableInformation()
        pt.LinuxPartitionName = ptEntry[0]
        pt.Start = ptEntry[1]
        pt.Size = ptEntry[2]
        return pt

    def parsePTEntry(self, ptEntry):
        ptEntry = ptEntry.rstrip()
        ptEntry = ptEntry.split(' ')

        pt = ptInformation()
        pt.name = ptEntry[1]
        pt.device = ptEntry[2]
        pt.instance = ptEntry[3]
        pt.Start = ptEntry[4]
        pt.End = ptEntry[5]
        pt.IsPersistent = ptEntry[6]
        return pt

    def FastFlashCheck(self, l_FileName):
        l_NewFileName = "old_bct.bin"

        cwd = os.getcwd()
        ReadCmd = "/tmp/nvdd --device /dev/block/3270000.spi --partsize 2888 --startoffset 0 --outputbin /tmp/" + l_NewFileName
        self.AdbShell(ReadCmd)
        self.AdbPull("/tmp/" + l_NewFileName, cwd)
        nvpt_cmd = self.flashUtils.f_Nvpt + " --bct " + l_NewFileName + " --chip 0x19 --out bct.txt"
        result = self.shellUtils.executeShellCommand(nvpt_cmd)
        if (result != nverror.NvError_Success):
            AbnormalTermination("s_ERROR_INVALID_PARAMS -- Flash Flash Equivalence Failed", nverror.NvError_InvalidArgument)

        # Dump PT
        l_NewFileName = "old_pt.bin"
        fileData = self.shellUtils.catFile("bct.txt")
        fileData = fileData.splitlines()
        for line in fileData:
            line = line.strip()
            if (not line or line[0] == '#'):
                continue

            pt = self.parsePartTableEntry(line)
            ReadCmd = "/tmp/nvdd --device " + pt.LinuxPartitionName + " --partsize " + pt.Size + " --startoffset " +  pt.Start + " --outputbin /tmp/" + l_NewFileName
            self.AdbShell(ReadCmd)
            self.AdbPull("/tmp/" + l_NewFileName, cwd)

        nvpt_cmd = self.flashUtils.f_Nvpt + " --pt " + l_NewFileName + " --chip 0x19 --out pt.txt"
        accum_pt = self.shellUtils.executeShellCommand(nvpt_cmd, True)

        count = 1
        fileData = self.shellUtils.catFile("pt.txt")
        fileData = fileData.splitlines()
        filelist = []
        for line in fileData:
            l_NewFileName = "pt" + str(count) + ".bin"
            line = line.strip()
            if (not line or line[0] == '#'):
                continue

            pt = self.parsePartTableEntry(line)
            ReadCmd = "/tmp/nvdd --device " + pt.LinuxPartitionName + " --partsize " + pt.Size + " --startoffset " +  pt.Start + " --outputbin /tmp/" + l_NewFileName
            self.AdbShell(ReadCmd)
            self.AdbPull("/tmp/" + l_NewFileName, cwd)
            nvpt_cmd = self.flashUtils.f_Nvpt + " --pt " + l_NewFileName + " --chip 0x19 --out " + "pt" + str(count) + ".txt"
            tmp_pt = self.shellUtils.executeShellCommand(nvpt_cmd, True)
            accum_pt = accum_pt + tmp_pt
            filelist.append("pt" + str(count) + ".txt")
            count = count + 1

        for i in filelist:
            fileData = self.shellUtils.catFile(i)
            fileData = fileData.splitlines()
            for line in fileData:
                l_NewFileName = "pt" + str(count) + ".bin"
                line = line.strip()
                if (not line or line[0] == '#'):
                    continue
                ReadCmd = "/tmp/nvdd --device " + pt.LinuxPartitionName + " --partsize " + pt.Size + " --startoffset " +  pt.Start + " --outputbin /tmp/" + l_NewFileName
                self.AdbShell(ReadCmd)
                self.AdbPull("/tmp/" + l_NewFileName, cwd)
                nvpt_cmd = self.flashUtils.f_Nvpt + " --pt " + l_NewFileName + " --chip 0x19 --out " + "pt" + str(count) + ".txt"
                tmp_pt = self.shellUtils.executeShellCommand(nvpt_cmd, True)
                accum_pt = accum_pt + tmp_pt
                if (result != nverror.NvError_Success):
                    AbnormalTermination("s_ERROR_INVALID_PARAMS -- Flash Flash Equivalence Failed", nverror.NvError_InvalidArgument)

        # wildcard expansion of man text files
        manlist = glob.glob('man_*.txt')
        cat_cmd = "cat "
        for i in manlist:
            cat_cmd = cat_cmd + i + ' '

        # Do the check
        manifest = self.shellUtils.executeShellCommand(cat_cmd, True)

        fileData = accum_pt
        fileData = fileData.splitlines()
        for line in fileData:
            line = line.strip()
            if (not line or line[0] == '#'):
                continue

            old_pt = self.parsePTEntry(line)
            l_Found = False

            newfileData = manifest
            newfileData = newfileData.splitlines()
            for newline in newfileData:
                newline = newline.strip()
                if (not newline or newline[0] == '#'):
                    continue

                new_pt = self.parsePTEntry(newline)
                if ((old_pt.name == new_pt.name) and
                    (old_pt.device == new_pt.device) and
                    (old_pt.instance == new_pt.instance) and
                    (old_pt.Start == new_pt.Start) and
                    (old_pt.End == new_pt.End)):
                    l_Found = True
                    break

        if (l_Found != True):
            AbnormalTermination("s_ERROR_INVALID_PARAMS -- Flash Flash Equivalence Failed " + old_pt.name, nverror.NvError_InvalidArgument)

    def getUFSPhyLaneFileValue(self, key):
        value = self.shellUtils.grep(self.targetConfig.boardDefaultPaths["f_UFSPhyLaneFile"], key)
        if(len(value) == 0):
            return None

        value = value[0].split('=')[1]
        value = value.rstrip()
        return value

    def getUfsProvisionCfgFileValue(self, key):
        value = self.shellUtils.grep(self.targetConfig.f_UfsProvisionCfg, key)
        if(len(value) == 0):
            return None

        value = value[0].split('=')[1]
        value = value.rstrip()
        return value

    # Function to Provision UFS Device
    # Program RefClock frequency and LUN's for UFS device
    # based on the UFS config file
    def ProvisionUFSDevice(self, TempDir):
        l_CmdFileName = "provision_cmds.txt"
        l_CmdFileNamePath = os.path.join(TempDir, l_CmdFileName)

        if (self.targetConfig.n_UFSProvisioningEnable == 1):
            if (os.path.isfile(self.targetConfig.f_UfsProvisionCfg) == False):
                self.vlog(self.targetConfig.f_UfsProvisionCfg + " file not found")
                AbnormalTermination("s_ERROR_FILE_NOT_EXIST", nverror.NvError_FileNotFound)

        l_UfsNode = self.getUfsProvisionCfgFileValue("UFS_debugfs_node")
        if (not l_UfsNode):
            self.vlog("UFS Node not there")
            AbnormalTermination("s_ERROR_UFS_PROVISION", nverror.NvError_ConfigVarNotFound)

        if (os.path.isfile(l_CmdFileNamePath)):
            self.shellUtils.removeFile(l_CmdFileNamePath)

        commandFile = open(l_CmdFileNamePath, 'w', 0o666)
        if(commandFile == -1):
            AbnormalTermination("Could not open " + commandFile, nverror.NvError_FileOperationFailed)

        l_ProgClkFreq = self.getUfsProvisionCfgFileValue("program_refclkfreq")
        l_refClkFreqVal = self.getUfsProvisionCfgFileValue("refclkfreq_value")
        l_ProgLun = self.getUfsProvisionCfgFileValue("program_lun")
        l_NumLuns = self.getUfsProvisionCfgFileValue("number_of_luns")
        l_BootEnable = self.getUfsProvisionCfgFileValue("boot_enable")
        l_DescrAccessEn = self.getUfsProvisionCfgFileValue("descr_access_en")
        l_ProgBootLunID = self.getUfsProvisionCfgFileValue("program_bootlun_en_id")
        l_BootLunIDVal = self.getUfsProvisionCfgFileValue("bootlun_en_id_value")
        l_EnableSharedWb = self.getUfsProvisionCfgFileValue("enable_shared_wb")
        l_SharedWbAllocUnits = self.getUfsProvisionCfgFileValue("shared_wb_alloc_units")

        if (l_ProgClkFreq and int(l_ProgClkFreq) == 1):

            l_ufsRefClkPath = "/sys/kernel/debug/" + l_UfsNode + "/ufs_refclk"

            if (l_refClkFreqVal):
                self.vlog("Set Clock Frequency = " + l_refClkFreqVal)
                buf = l_refClkFreqVal + " " + l_ufsRefClkPath + "/refclkfreq_value\n"
                buf += "1 " + l_ufsRefClkPath + "/program_refclkfreq\n"
                commandFile.write(buf)
            else:
                self.vlog("No Refclk Frequency Value provided")
                AbnormalTermination("s_ERROR_UFS_PROVISION", nverror.NvError_ConfigVarNotFound)

        if (l_ProgBootLunID and int(l_ProgBootLunID) == 1):
            l_ufsBootLunIDPath = "/sys/kernel/debug/" + l_UfsNode + "/ufs_bootlun_en_id"

            if (l_BootLunIDVal):
                self.vlog("Set BootLUN ID = " + l_BootLunIDVal)
                buf = l_BootLunIDVal + " " + l_ufsBootLunIDPath + "/bootlun_en_id\n"
                buf += "1 " + l_ufsBootLunIDPath + "/program_bootlun_en_id\n"
                commandFile.write(buf)

            else:
                self.vlog("No BootLUN ID Value provided")
                AbnormalTermination("s_ERROR_INVALID_ARGUMENT", nverror.NvError_ConfigVarNotFound)

        if (l_ProgLun and int(l_ProgLun) == 1):
            l_ufsLunPath = "/sys/kernel/debug/" + l_UfsNode + "/ufs_luns"
            buf = ""
            if (l_BootEnable):
                self.vlog("Set bBootEnable = " + str(l_BootEnable))
                buf += l_BootEnable + " " + l_ufsLunPath + "/boot_enable\n"

            if (l_DescrAccessEn):
                self.vlog("Set bDescrAccessEn = " + l_DescrAccessEn)
                buf += l_DescrAccessEn + " " + l_ufsLunPath + "/descr_access_en\n"

            if (l_EnableSharedWb != None) and (l_SharedWbAllocUnits != None):
                self.vlog("EnabledSharedWb = " + l_EnableSharedWb)
                self.vlog("SharedWbAllocUnits = " + l_SharedWbAllocUnits)
                buf += l_EnableSharedWb + " " + l_ufsLunPath + "/enable_shared_wb\n"
                buf += l_SharedWbAllocUnits + " " + l_ufsLunPath + "/shared_wb_alloc_units\n"

            commandFile.write(buf)

            if (l_NumLuns and int(l_NumLuns) != 0):
                self.vlog("Number of LUNS : " + l_NumLuns)
                for i in range(0, int(l_NumLuns) - 1):
                    buf = ""
                    l_bLUEnable = self.getUfsProvisionCfgFileValue("lun" + str(i) + ".bLUEnable")
                    if (not l_bLUEnable):
                        l_bLUEnable = "0"
                    elif(int(l_bLUEnable, 16) == 1):
                        self.vlog("Program LUN enabled for lun " + str(i))

                        buf += l_bLUEnable + " " + l_ufsLunPath + "/lun" + str(i) + "/bLUenable\n"

                        l_bBootLunID = self.getUfsProvisionCfgFileValue("lun" + str(i) + ".bBootLunID")
                        if (l_bBootLunID):
                            buf += l_bBootLunID + " " + l_ufsLunPath + "/lun" + str(i) + "/bBootLUNID\n"

                        l_bLUWriteProtect = self.getUfsProvisionCfgFileValue("lun" + str(i) + ".bLUWriteProtect")
                        if (l_bLUWriteProtect):
                            buf += l_bLUWriteProtect + " " + l_ufsLunPath + "/lun" + str(i) + "/bLUWriteProtect\n"

                        l_bMemoryType = self.getUfsProvisionCfgFileValue("lun" + str(i) + ".bMemoryType")
                        if (l_bMemoryType):
                            buf += l_bMemoryType + " " + l_ufsLunPath + "/lun" + str(i) + "/bMemoryType\n"

                        l_dNumAllocUnits = self.getUfsProvisionCfgFileValue("lun" + str(i) + ".dNumAllocUnits")
                        if (l_dNumAllocUnits):
                            self.vlog("Num Allock Units: " + str(l_dNumAllocUnits))
                            buf += l_dNumAllocUnits + " " + l_ufsLunPath + "/lun" + str(i) + "/dNumAllocUnits\n"

                        l_bDataReliability = self.getUfsProvisionCfgFileValue("lun" + str(i) + ".bDataReliability")
                        if (l_bDataReliability):
                            buf += l_bDataReliability + " " + l_ufsLunPath + "/lun" + str(i) + "/bDataReliability\n"

                        l_bLogicalBlockSize = self.getUfsProvisionCfgFileValue("lun" + str(i) + ".bLogicalBlockSize")
                        if (l_bLogicalBlockSize):
                            buf += l_bLogicalBlockSize + " " + l_ufsLunPath + "/lun" + str(i) + "/bLogicalBlocksize\n"

                        l_bProvisioningType = self.getUfsProvisionCfgFileValue("lun" + str(i) + ".bProvisioningType")
                        if (l_bProvisioningType):
                            buf += l_bProvisioningType + " " + l_ufsLunPath + "/lun" + str(i) + "/bProvisionType\n"

                        self.vlog("Set bBootLUNID:" + str(l_bBootLunID) + ", bLUWriteProtect:" + str(l_bLUWriteProtect))
                        self.vlog("Set bMemoryType:" + str(l_bMemoryType) + ", dNumAllocUnits:" + str(l_dNumAllocUnits))
                        self.vlog("Set bDataReliability:" + str(l_bDataReliability) + ", bLogicalBlocksize:" + str(l_bLogicalBlockSize))
                        self.vlog("Set bProvisionType:" + str(l_bProvisioningType))

                    commandFile.write(buf)

                commandFile.write("1 " + l_ufsLunPath + "/program_lun\n")
                commandFile.close()
                self.AdbPush(l_CmdFileNamePath)
                if (self.AdbShell("/tmp/nvdd -d /dev/block/a80b8d0000.ufshci -l /tmp/" + l_CmdFileName) != nverror.NvError_Success):
                    AbnormalTermination("Provision Failed", nverror.NvError_FileWriteFailed)
                self.AdbShell("rm /tmp/" + l_CmdFileName)
                self.shellUtils.removeFile(l_CmdFileNamePath)
                self.vlog("UFS Provisioning Done. Please Power cycle your device")
            else:
                self.vlog("No LUN's to Provision")
                commandFile.close()
                AbnormalTermination("s_ERROR_UFS_PROVISION -- No LUN's to Provision", nverror.NvError_ConfigVarNotFound)

