#!/usr/bin/python
# SPDX-FileCopyrightText: Copyright (c) 2017-2025, NVIDIA CORPORATION.  All rights reserved.
# SPDX-License-Identifier: LicenseRef-NvidiaProprietary
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

import os
import sys
import subprocess
import glob
import stat
import shutil
import re
import inspect
from flashtools_nverror import nverror
from flashtools_nverror import AbnormalTermination
import itertools
from multiprocessing import Pool


class flash_utilities(object):
    f_AdbTool = "adb"
    f_TegraSign = ["tegrasign_v3.py", "tegrasign_v3_internal.py" , "tegrasign_v3_util.py", \
        "tegrasign_v3_hsm.py", "tegraopenssl", "tegrasign_v3_oemkey_t234.yaml", "xmss-sign",\
        "tegrasign_v3_oemkey_t264.yaml"]
    f_TegraSignPriv = ["tegrasign_v3_nvkey_load.py", "tegrasign_v3_nvkey.yaml"]
    f_TegraSignDevKeyFiles = ["t234_sbk_dev.key", "t234_rsa_dev.key"]
    f_TegraBct = "tegrabct_v2"
    f_NvSkuInfo = "nvskuinfo"
    f_NvBchValidate = "nvbchvalidate"
    f_TegraRcm = "tegrarcm_v2"
    f_NvImageGen = "nvimagegen"
    f_NvImageSign = "nvimagesign"
    f_NvSimgDump = "nvsimgdump"
    f_NvDDTool = "nvdd"
    f_NvOptinFuse = "optin_fuse"
    f_CompressLz4 = "flash_lz4"
    f_Nvdtoverlay = "nvdtoverlay"
    f_Nvpt = "nvpt"
    f_TegraHost = "tegrahost_v2"
    f_TegraParser = "tegraparser_v2"
    f_NvResign = "nvresign"
    p_FlashPath = None

    def __init__(self, flashPath):

        if(not os.path.isdir(flashPath)):
            self.vlog("invalid flash path")
            raise OSError

        self.setFlashUtilPaths(flashPath)

    def vlog(self, msg):
        curframe = inspect.currentframe()
        stack = inspect.getouterframes(curframe, 2)
        caller = stack[1][3]
        lineNum = stack[1][2]
        print("[" + caller + "(" + str(lineNum) + ")] : " + msg)

    def setFlashUtilPaths(self, path):
        os_suffix = ""
        if (os.name == "nt"):
            os_suffix = ".exe"

        self.p_FlashPath = path
        self.f_AdbTool = os.path.join(self.p_FlashPath, self.f_AdbTool + os_suffix)
        self.f_CompressLz4 = os.path.join(self.p_FlashPath, self.f_CompressLz4 + os_suffix)
        self.f_TegraBct = os.path.join(self.p_FlashPath, self.f_TegraBct + os_suffix)
        for i in range(len(self.f_TegraSign)):
            self.f_TegraSign[i] = os.path.join(self.p_FlashPath, self.f_TegraSign[i] + os_suffix)
        for i in range(len(self.f_TegraSignPriv)):
            self.f_TegraSignPriv[i] = os.path.join(self.p_FlashPath, self.f_TegraSignPriv[i] + os_suffix)
        for i in range(len(self.f_TegraSignDevKeyFiles)):
            self.f_TegraSignDevKeyFiles[i] = os.path.join(self.p_FlashPath, self.f_TegraSignDevKeyFiles[i] + os_suffix)
        self.f_TegraRcm = os.path.join(self.p_FlashPath, self.f_TegraRcm + os_suffix)
        self.f_TegraParser = os.path.join(self.p_FlashPath, self.f_TegraParser + os_suffix)
        self.f_NvSkuInfo = os.path.join(self.p_FlashPath, self.f_NvSkuInfo + os_suffix)
        self.f_NvBchValidate = os.path.join(self.p_FlashPath, self.f_NvBchValidate + os_suffix)
        self.f_NvImageGen = os.path.join(self.p_FlashPath, self.f_NvImageGen + os_suffix)
        self.f_NvImageSign = os.path.join(self.p_FlashPath, self.f_NvImageSign + os_suffix)
        self.f_NvSimgDump = os.path.join(self.p_FlashPath, self.f_NvSimgDump + os_suffix)
        self.f_NvDDTool = os.path.join(self.p_FlashPath, self.f_NvDDTool + os_suffix)
        self.f_NvOptinFuse = os.path.join(self.p_FlashPath, self.f_NvOptinFuse + os_suffix)
        self.f_Nvdtoverlay = os.path.join(self.p_FlashPath, self.f_Nvdtoverlay + os_suffix)
        self.f_Nvpt = os.path.join(self.p_FlashPath, self.f_Nvpt + os_suffix)

class shell_utilities(object):
    sysMonitor = None
    cmdOutput = ""
    exceptlog = None
    def __init__(self, sysMon=None):
        self.sysMonitor = sysMon
        self.exceptlog = True

    def formatLogMessage(self, msg):
        try:
            curframe = inspect.currentframe()
            stack = inspect.getouterframes(curframe, 2)
            caller = stack[2][3]
            lineNum = stack[2][2]
            msg ="[" + str(caller) + "(" + str(lineNum) + ")] : " + str(msg)
        except:
            pass
        return msg

    def log(self, msg, verbose=False):
        msg = self.formatLogMessage(msg)
        if(self.sysMonitor != None):
            if(self.sysMonitor.debuging) or (verbose == True):
                self.sysMonitor.log(msg, True)
            else:
                self.sysMonitor.log(msg, False)
        sys.stdout.flush()

    def vlog(self, msg):
        msg = self.formatLogMessage(msg)
        if(self.sysMonitor != None):
            self.sysMonitor.log(msg, True)
        else:
            print(msg)
        sys.stdout.flush()

    def setMonitor(self, sysMon):
        self.sysMonitor = sysMon

    def extractError(self, output):
        listFromOutput = output.split("\n")
        identifier = ":[E]:"
        extractError = False
        ErrorList = []

        for line in listFromOutput:
            if identifier in line:
                extractError = True
                ErrorList.append("\033[01;31m" + line + "\033[0m")

        if extractError == True:
            self.cmdOutput = '\n'.join(ErrorList) + '\n'
        else:
            self.cmdOutput = "\033[01;31m" + output + "\033[0m"

    def printError(self):
        print(self.cmdOutput)

    def executeShellCommand(self, command, returnOutput=False, debug=False):
        enviornment = os.environ.copy()
        if ('PYTHONPATH' in enviornment):
            del enviornment['PYTHONPATH']
        if((self.sysMonitor != None) and (self.sysMonitor.debuging == True)):
            debug = True

        self.log("command = " + command + "\ncwd = " + os.getcwd(), debug)

        commandList = command.split()
        try:
            output = subprocess.check_output(commandList, env=enviornment, stderr=subprocess.STDOUT)
            output = str(output.decode("utf-8"))

            self.log(output, debug)

            if(returnOutput):
                return output
            else:
                #no exceptions reuturn success
                return nverror.NvError_Success
        except Exception as e:
            self.vlog("shell command -- " + command + " failed")
            if (hasattr(e, 'output')):
                self.vlog("Output: %s" % str(e.output.decode("utf-8")))
            if (hasattr(e, 'returncode')):
                self.vlog("Return code: %d" % (e.returncode))
            if (hasattr(e, 'stdout')):
                self.vlog("stdout: %s" % (e.stdout))
            if (hasattr(e, 'stderr')):
                self.vlog("stderr: %s" % (e.stderr))
            return nverror.NvError_Unknown

    def executeParallelShellCommands(self, commands, returnOutput=False, debug=False):
        if((self.sysMonitor != None) and (self.sysMonitor.debuging == True)):
            debug = True

        commandOutput = []
        processList = []
        for command in commands:
            commandList = command.split()

            self.log("command = " + command + "\ncwd = " + os.getcwd(), debug)

            try:
                p = subprocess.Popen(commandList, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            except Exception as err:
                self.log(str(err), self.exceptlog)
                return nverror.NvError_Unknown

            processList.append((p, command))

        # Wait for all the processes to complete
        for process, command in processList:
            stdoutData, stdErrData = process.communicate()

            output = str(stdoutData.decode("utf-8"))
            if (stdErrData):
                error = str(stdErrData.decode("utf-8"))
            else:
                error = ""

            self.log(output, debug)

            if (process.returncode != 0):
                # Error when running the command
                self.vlog("shell command -- " + command + " failed.\n")
                self.vlog("Output: " + str(stdoutData) + "\n")
                self.vlog("Error: " + error + "\n")
                return nverror.NvError_Unknown
            else:
                if(returnOutput):
                    commandOutput.append(output)

        if(returnOutput):
            return commandOutput
        else:
            #no exceptions reuturn success
            return nverror.NvError_Success

    # https://stackoverflow.com/questions/8110310/simple-way-to-query-connected-usb-devices-info-in-python
    def lsusb(self):
        device_re = re.compile(r"Bus\s+(?P<bus>\d+)\s+Device\s+(?P<device>\d+).+ID\s(?P<id>\w+:\w+)\s(?P<tag>.+)$", re.I)
        df = subprocess.check_output("lsusb").decode("utf-8")
        devices = {}
        for i in df.split('\n'):
            if i:
                info = device_re.match(i)
                if info:
                    dinfo = info.groupdict()
                    path = '/dev/bus/usb/%s/%s' % (dinfo['bus'], dinfo['device'])

                    devices.update({path:dinfo})

        return devices

    def touch(self, path):
        with open(path, 'a'):
            os.utime(path, None)

    def catFile(self, path):
        if(not path):
            errorStr = "Empty path given as parameter"
            AbnormalTermination(errorStr, nverror.NvError_BadParameter)

        if(not os.path.exists(path) or not os.path.isfile(path)):
            errorStr = "Invalid path " + path
            AbnormalTermination(errorStr, nverror.NvError_FileNameNotExist)

        fid = open(path, "r")
        if(fid == -1):
            errorStr = "Failed to open " + path
            AbnormalTermination(errorStr, nverror.NvError_FileOperationFailed)

        data = str(fid.read())
        fid.close()
        return data

    def grepFileWithRegex(self, filePath, searchRegex):
        with open(filePath, "r") as fileIn:
            fileContent = fileIn.read()
            return self.grepStringWithRegex(fileContent, searchRegex)

    def grepStringWithRegex(self, string, searchRegex):
        m = re.search(searchRegex, string)
        return m

    def replaceString(self, searchString, replaceString, filePath):
        fileContent = None
        with open(filePath, "r") as fileIn:
            fileContent = fileIn.read()
            fileContent = fileContent.replace(searchString, replaceString)

        with open(filePath, "w") as fileOut:
            fileOut.write(fileContent)

    def grep(self, path, searchString, fileName=True):
        if(not searchString):
            return None
        elif(not path):
            errorStr = "Empty path given as parameter"
            AbnormalTermination(errorStr, nverror.NvError_BadParameter)

        if(fileName):
            if(not os.path.exists(path) or not os.path.isfile(path)):
                errorStr = "Invalid path " + path
                AbnormalTermination(errorStr, nverror.NvError_FileNameNotExist)
            else:
                data = self.catFile(path)
        else:
            data = path

        lineArray = data.splitlines()
        match = []
        for line in lineArray:
            if(not line or line[0] == '#'):
                continue

            line = line.split('#')[0]

            if(line.find(searchString) != -1):
                match.append(line)

        return match

    def grepInFolder(self, path, searchString, printOut=False):
        grepOut = []
        grepOutInFolder= []
        self.log ("searchString : " + searchString)
        if(not os.path.exists(path) or not os.path.isdir(path)):
            errorStr = "Invalid path " + path
            AbnormalTermination(errorStr, nverror.NvError_FileNameNotExist)
        fileList = os.listdir(path)
        for filename in fileList:
            if (not os.path.exists(filename)):
                continue
            filepath = path + filename
            self.log(filepath, printOut)
            grepOut = self.grep(filepath, searchString)
            if(grepOut != None):
                self.log(grepOut,printOut)
                grepOutInFolder = grepOutInFolder +  grepOut
        return grepOutInFolder

    def removeFile(self, path):
        try:
            # self.vlog("remove " + path + " called")
            files = glob.glob(path)
            for f in files:
                try:
                    os.unlink(f)
                except:
                    self.vlog("could not remove file " + f)
        except Exception as e:
            self.vlog("Could not remove file " + str(path))
            self.vlog(str(e))

    def ls(self, path):
        out = self.executeShellCommand("ls " + path, True)
        self.vlog(out)

    def makeDirectory(self, path):
        # self.targetConfig.sysMonitor.log(sys._getframe().f_code.co_name + " entered")
        if(not path):
            errorStr = "Empty path given as parameter"
            AbnormalTermination(errorStr, nverror.NvError_BadParameter)

        if(os.path.exists(path) and os.path.isdir(path)):
            return
        original_umask = os.umask(0)
        try:
            os.makedirs(path)
        except Exception as e:
            errorStr = "failed to make directory " + path + "\n" + str(e)
            AbnormalTermination(errorStr, nverror.NvError_FileOperationFailed)
        finally:
            os.umask(original_umask)
        return

    def appendFile(self, path, stringToAppend):

        if(not path):
            errorStr = "Empty path given as parameter"
            AbnormalTermination(errorStr, nverror.NvError_BadParameter)

        if(not os.path.exists(path)):
            self.touch(path)

        with open(path, "a+") as fileToAppend:
            fileToAppend.write(stringToAppend)

    def appendBinary(self, parentBinary, binaryToAppend):
        try:
            with open(parentBinary, "ab") as binfile, open(binaryToAppend, "rb") as blob:
                binfile.write(blob.read())
        except:
            msg = "Failed to concatenate " + parentBinary + " with " + binaryToAppend
            AbnormalTermination(msg, nverror.NvError_FileOperationFailed)

    def Copy(self, l_f_source, l_f_destination, l_verbose=False, executable=False, ignore_pattern='', overwrite=True):
        if(not l_f_source or not l_f_destination):
            errorMsg = "L_F_SOURCE : invalid source/destination -- source=" + str(l_f_source) + " destination=" + str(l_f_destination)
            AbnormalTermination(errorMsg, nverror.NvError_FileOperationFailed)

        if (l_f_source == l_f_destination):
            return

        if (l_f_source.find('*') != -1):
            errorMsg = "Source file contains * in its name, invalid!"
            AbnormalTermination(errorMsg, nverror.NvError_InvalidArgument)

        if (not os.path.exists(l_f_source)):
            errorMsg = l_f_source + " does not exist"
            AbnormalTermination(errorMsg, nverror.NvError_FileNotFound)

        try:
            if overwrite:
                if(os.path.isdir(l_f_source)):
                    if(os.path.isdir(l_f_destination)):
                        shutil.rmtree(l_f_destination)
                    shutil.copytree(l_f_source, l_f_destination, ignore=shutil.ignore_patterns(ignore_pattern))
                    return
                else:
                    if (os.path.isdir(l_f_destination)):
                        baseName = os.path.basename(l_f_source)
                        l_f_destination += "/" + baseName
                    shutil.copyfile(l_f_source, l_f_destination)
            else:
                self.copyfoldernooverwrite(l_f_source, l_f_destination )

            if(executable):
                os.chmod(l_f_destination, stat.S_IRWXU | stat.S_IRGRP | stat.S_IXGRP | stat.S_IROTH | stat.S_IWOTH)
            else:
                os.chmod(l_f_destination, stat.S_IREAD | stat.S_IWRITE | stat.S_IRGRP | stat.S_IWGRP | stat.S_IROTH | stat.S_IWOTH)

            #if (l_verbose):
            #    self.vlog("Copy " + l_f_source + " to " + l_f_destination)

            return
        except Exception as e:
            self.vlog("Unable to copy file " + l_f_source + " to " + l_f_destination)
            self.vlog("Exception thrown " + str(e))
            AbnormalTermination(str(e), nverror.NvError_FileOperationFailed)

    def copyfoldernooverwrite(self, source, destination):
        for element in os.listdir(source):
            srcpath = os.path.join(source, element)
            destpath = os.path.join(destination, element)
            if os.path.isdir(srcpath):
                self.copyfoldernooverwrite(srcpath, destpath)
            else:
                if not os.path.exists(destpath):
                    shutil.copy2(srcpath, destpath)

    def CopyVerbose(self, source, destination):
        self.Copy(source, destination , True)

    def str2bool(self, argument):
        if(argument.lower() == "true"):
            return True
        elif(argument.lower() == "false"):
            return False
        else:
            AbnormalTermination("Invalid argument for str2bool --" + str(argument), nverror.NvError_InvalidArgument)

    def CopyifSrcisafile(self, source, dest, executable=False):
        if (os.path.isfile(source)):
            self.Copy(source, dest, executable=executable)
        else:
            self.log("No source file found : " + source)

    def find(self, searchPath, searchFile, search_links=False):
        if(searchPath == None or searchFile == None):
            self.vlog("invalid path to find utility")
            self.vlog("searchPath =" + str(searchPath))
            self.vlog("file = " + str(searchFile))
            AbnormalTermination("invalid Path", nverror.NvError_BadValue)

        if(os.path.isdir(searchPath) == False):
            self.vlog(f"search path not a directory -{searchPath}")
            AbnormalTermination(f"Not a directory -- {searchPath}", nverror.NvError_BadValue)

        filesFound = []

        for root, directories, filenames in os.walk(searchPath, followlinks=search_links):
            for f in filenames:
                if(f.find(searchFile) != -1):
                    filesFound += [root + "/" + f]

        return filesFound
