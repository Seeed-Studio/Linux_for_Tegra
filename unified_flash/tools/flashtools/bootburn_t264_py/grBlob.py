#!/usr/bin/python
# Copyright (c) 2024, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.


import sys
import os
from flash_utilities import shell_utilities
from flashtools_nverror import nverror, AbnormalTermination
from system_monitor import monitor

class grBlob():
    flahpath = ""
    OutDir = ""
    f_NvGrBlobGen = "nvgrblobgen"
    f_NvMbGrBlobImage  = "MbGrBlob.bin"
    f_NvISTGrBlobImage = "ISTGrBlob.bin"
    shellUtils = shell_utilities()
    Monitor = monitor()

    def __init__(self, flashpath, outdir):
        self.OutDir = outdir
        self.f_NvGrBlobGen = os.path.abspath(os.path.join(flashpath, self.f_NvGrBlobGen))

    def AppendBlobToBin(self, BlobImage, BinImage, p_TempDumpPath):
        if(os.path.isfile(BinImage) == False):
            AbnormalTermination("File not found " + BinImage, nverror.NvError_FileNotFound)
        elif(os.path.isdir(p_TempDumpPath) == False):
            AbnormalTermination("Invalid path " + p_TempDumpPath, nverror.NvError_FileNotFound)

        self.Monitor.vlog("Appending Blob = " + os.path.join(p_TempDumpPath,BlobImage) + " to binary = " + BinImage)
        try:
            with open(BinImage, "ab") as binfile, open(os.path.join(p_TempDumpPath,BlobImage), "rb") as blob:
                binfile.write(blob.read())
        except:
            msg = "Failed to concatenate " + BinImage + " with " + os.path.join(p_TempDumpPath,BlobImage)
            AbnormalTermination(msg, nverror.NvError_FileOperationFailed)

        return BinImage

    def AppendBlBlobs(self, mb2_bins, ist_bins):
        mb2_bins = list(set(mb2_bins))
        ist_bins = list(set(ist_bins))
        self.Monitor.vlog("Appending Blob for MB2")
        for mb2_bin in mb2_bins:
            self.AppendBlobToBin(self.f_NvMbGrBlobImage, mb2_bin, self.OutDir)
        self.Monitor.vlog("Appending Blob for IST")
        for ist_bin in ist_bins:
            self.AppendBlobToBin(self.f_NvISTGrBlobImage, ist_bin, self.OutDir)

    def getCSVfile(self, path, boardType, bootType, boardID):
        csvDirectoryFiles = [f for f in os.listdir(path) if os.path.isfile(os.path.abspath(os.path.join(path, f)))]
        for f in csvDirectoryFiles:
            if(boardType.upper() in f and bootType.upper() in f and boardID.upper() in f):
                return f
        #Could not find it
        return None

    def GenerateGrBlobs(self, mediaCombos, p_NvGrCsvPath, s_BoardName, boardDefaultPaths):
        f_CsvName = ""
        l_NvGrBlobGenCmd = ""
        s_GrCsvString = None
        s_BoardId = s_BoardName[6:8]
        if("s_GrCsvString" in boardDefaultPaths):
            s_GrCsvString = boardDefaultPaths["s_GrCsvString"]
       # if ( not s_GrCsvString or not mediaCombos):
       #     print("GR Blob generation is unsupported for this platform")
       #     AbnormalTermination("GR Blob unsupported on " + s_BoardName, nverror.NvError_NotSupported)

        print("Generating GR Blobs")
        for grType in ['MB1', 'MB2', 'IST']:

            f_CsvName = self.getCSVfile(p_NvGrCsvPath, s_GrCsvString, grType, s_BoardId)
            if(f_CsvName == None):
                AbnormalTermination("Could not find " + grType + " CSV file for " + s_GrCsvString + " in " + p_NvGrCsvPath, nverror.NvError_NotSupported)

            f_CsvName = os.path.abspath(os.path.join(p_NvGrCsvPath, f_CsvName))
            l_NvGrBlobGenCmd += " --" + grType.lower() + "csv " + f_CsvName
            if(grType in mediaCombos):
                if grType=='IST':
                    l_NvGrBlobGenCmd += " --" + grType.lower() + "mediaIntf SDMMC4_IMPL"
                    continue
                l_NvGrBlobGenCmd += " --" + grType.lower() + "mediaIntf " + mediaCombos[grType]
        l_NvGrBlobGenCmd = self.f_NvGrBlobGen + l_NvGrBlobGenCmd
        l_NvGrBlobGenCmd +=  " --outdir " + self.OutDir + "/"

        result = self.shellUtils.executeShellCommand(l_NvGrBlobGenCmd)
        if(result != nverror.NvError_Success):
            AbnormalTermination("\033[01;31m FAILED to generate GR Blob unsupported on \033[0m", nverror.NvError_SystemCommand)

        self.Monitor.vlog("\033[01;32m Successfully generated GR blobs \033[0m")
