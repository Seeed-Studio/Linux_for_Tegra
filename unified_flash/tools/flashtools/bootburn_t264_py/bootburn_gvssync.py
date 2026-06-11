#!/usr/bin/python
# Copyright (c) 2017-2018, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

import os
import shutil
import time
import stat
import inspect
import importlib
import traceback
import json
import sys

from flash_utilities import shell_utilities
from flashtools_nverror import nverror
from flashtools_nverror import AbnormalTermination

class bootburn_gvscontainer():
    # Synchronize multiple bootburn instances in GVS testing  setup
    # In container setup, synchronize  among multiple Tegra Instances
    # Synchronize with GVS Infra in both container and native host env
    # Use the host_config.json to read the lock file
    # Presence of this lock file indicates tegra recovery is in progress
    # Absence this lock file indicates tegra recovery is not in progress

    inGVSenv = False
    shellUtils = shell_utilities()
    targetConfig = None
    debugging = False

    def __init__(self, targetConfig):
        self.targetConfig = targetConfig
        #self.bootburnLib = lib
        if(self.targetConfig.TEST_AUTOMATION == True) and (not "create_bsp_images.py" in  os.path.basename(sys.argv[0])):
            self.inGVSenv = True
        if(targetConfig.parsed_commandline and targetConfig.parsed_commandline["D"]):
            self.debugging = True
            targetConfig.sysMonitor.debuging = True

    def formatLogMessage(self, msg):
        curframe = inspect.currentframe()
        stack = inspect.getouterframes(curframe, 2)
        caller = stack[2][3]
        lineNum = stack[2][2]
        msg ="[" + caller + "(" + str(lineNum) + ")] : " + msg
        return msg

    def log(self, msg):
        msg = self.formatLogMessage(msg)
        if(self.debugging):
            self.targetConfig.sysMonitor.log(msg, True)
        else:
            self.targetConfig.sysMonitor.log(msg, False)
        sys.stdout.flush()

    def vlog(self, msg):
        msg = self.formatLogMessage(msg)
        self.targetConfig.sysMonitor.log(msg, True)
        sys.stdout.flush()

    def __enter__(self):
        if self.inGVSenv == True:
            self.LockTegrasStateChange()
        return self

    def __exit__(self, except_type, except_value, except_tb):
        if self.inGVSenv == True:
            self.UnlockTegrasStateChange()
        if(except_type is None):
            return True
        else:
            self.vlog("Exception in critical section :" + str(except_type))
            return False

    def readHarnessLockFile(self):
        try:
            if os.path.exists(self.targetConfig.f_AutoLock):
                with open(self.targetConfig.f_AutoLock, "rt") as harnesslockfd:
                    self.vlog("Contents of  " + self.targetConfig.f_AutoLock + " :  " +  harnesslockfd.read())
        except Exception as lockreadwhilewait:
            self.vlog("Lock file read error while waiting for Lock file removal")
            self.vlog("with exception details :: " + str(lockreadwhilewait))

    def checkAndUnlockTegrasStateChange(self):
        lockIgnoreTime = 130
        try:
            if os.path.exists(self.targetConfig.f_AutoLock):
                harnessLockTime = time.time() - os.path.getctime(self.targetConfig.f_AutoLock)
                self.vlog("harnessLockFile creation Time = {}".format(harnessLockTime))
                if (harnessLockTime > lockIgnoreTime):
                    self.log("Harness Lock file is more than two minustes old, created at " + str(harnessLockTime))
                    self.readHarnessLockFile()
                    self.UnlockTegrasStateChange()
                else:
                    self.log("harnesslock file is recent, wait for unlock")
            else:
                self.log("harnessLock File is not available, no need to wait to Lock")

        except Exception as lockreadwhilewait:
            self.vlog("Exception in Lock file read and unlock in checkAndUnlockTegrasStateChange")
            self.vlog("with exception details :: " + str(lockreadwhilewait))

    def LockTegrasStateChange(self):
        sync_harness = False
        lockWait = True
        num_of_max_attempts =122
        attempt_num = 0
        harnesslockfd = None
        try:
            self.vlog("check and wait for Auto Lock file  " + self.targetConfig.f_AutoLock + ", if present")
            self. checkAndUnlockTegrasStateChange()
            while  lockWait:
                 #GVS or other bootburn instance is managing Tegra mode change
                 #reattempt for 60 secs with 1 sec sleep
                if os.path.exists(self.targetConfig.f_AutoLock):
                    self.readHarnessLockFile()
                    time.sleep(1)
                    attempt_num += 1
                    if attempt_num >= num_of_max_attempts:
                        lockWait = False
                        command = "ls -la " + str(self.targetConfig.f_AutoLock)
                        self.shellUtils.executeShellCommand(command, True, True)
                        self.vlog(str(os.lstat(self.targetConfig.f_AutoLock)))
                else:
                #create harness lock file
                    self.vlog("waited " + str(attempt_num)  + " sec to get the Auto Lock removed")
                    try:
                        harnesslockfd = os.open(self.targetConfig.f_AutoLock, os.O_EXCL|os.O_CREAT|os.O_RDWR)
                        os.write(harnesslockfd, (str.encode("locked by bootburn in " +  str(os.uname()[1]))))
                        os.close(harnesslockfd)
                        self.log("created Lock File : " + str(self.targetConfig.f_AutoLock))
                        lockWait = False
                        sync_harness = True
                    except Exception as openLockFileExcept:
                        self.vlog("Fail to create lock file, " + str(self.targetConfig.f_AutoLock))
                        self.vlog("with exception details :: " + str(openLockFileExcept))
        except Exception as lockfilecreateerr:
            self.vlog("exception in creating lockfile :: " + str(lockfilecreateerr))
            self.vlog("with exception details :: " + str(lockfilecreateerr))

        if sync_harness==False:
                AbnormalTermination(("Error in locking harness-lockfile  with " + str(num_of_max_attempts) +  " sec wait. escalate to GVS team "), nverror.NvError_ResourceError)

    def UnlockTegrasStateChange(self):
        try:
            if os.path.exists(self.targetConfig.f_AutoLock):
                #delete the harnessLockFilePath to unlock the aurix access
                os.remove(self.targetConfig.f_AutoLock)
                self.vlog(str((self.targetConfig.f_AutoLock) +  "   is deleted"))
            else:
                self.vlog(str((self.targetConfig.f_AutoLock) +  "  file doesn't exist"))
        except Exception as unlockFile:
            self.vlog("error in removing the lock file" + self.targetConfig.f_AutoLock)
            self.vlog("with exception details :: " + str(unlockFile))

    def GetAutoLockFilePath(self):
        #read the host_config.json to get harness_lock
        if self.inGVSenv == False:
            return True

        harness_lock = False
        harnessLockFilePath = None
        harnesslocks = None
        hostname_found = False
        hostname_index = 0
        hostname = os.uname()[1]
        f_AutoHostCfg = self.targetConfig.getEnviornmetalVariable("HOME") + "/hostconfig.json"
        self.vlog("hostname :: " + str(hostname))
        self.vlog("f_AutoHostCfg :: " + str(f_AutoHostCfg))

        try:
            if os.path.exists(f_AutoHostCfg):
                if os.path.isfile(f_AutoHostCfg):
                    with open(f_AutoHostCfg, "r") as autoHostCfg:
                        hostCfgData = autoHostCfg.read()
                        hostcfgJsonData = json.loads(hostCfgData)
                else:
                    self.vlog(f_AutoHostCfg + "is not a file")
            else:
                self.vlog(str(f_AutoHostCfg) + " is not found on this Automation setup")
            if not hostcfgJsonData:
                AbnormalTermination("Failed to find and load hostconfig.hson file : " + l_LockCfgFile, nverror.NvError_ResourceError)


            if "vrl" in hostcfgJsonData:
                if "vrl_slaves" in hostcfgJsonData["vrl"]:
                    for match_index in range(len(hostcfgJsonData["vrl"]["vrl_slaves"])):
                        if hostname in hostcfgJsonData["vrl"]["vrl_slaves"][match_index]["host_id"]:
                            hostname_found = True
                            hostname_index = match_index
                            self.vlog("hostname :: " + hostname)
                    if hostname_found:
                        if "harness_locks" in  hostcfgJsonData["vrl"]["vrl_slaves"][hostname_index]:
                            if "harness_recovery" in  hostcfgJsonData["vrl"]["vrl_slaves"][hostname_index]["harness_locks"]:
                                self.targetConfig.f_AutoLock = hostcfgJsonData["vrl"]["vrl_slaves"][hostname_index]["harness_locks"]["harness_recovery"]
                                self.vlog("harnessLockFilePath  : " + self.targetConfig.f_AutoLock)
                                if (len(self.targetConfig.f_AutoLock) != 0):
                                    harness_lock = True
                            else:
                                self.vlog("harness_recovery entry not found in " + f_AutoHostCfg)
                        else:
                             self.vlog("harness_lock_path not found in host config in Automation")
                    else:
                        self.vlog("host_name not found in :: " + f_AutoHostCfg)
                else:
                    self.vlog("vrl_slaves not found in host config in ")
            else:
                self.vlog("vrl not found in host config in Automation")

        except Exception as lockfileerr:
            print("exception in Automation Lock  sync :: " + str(lockfileerr))
            self.vlog("with exception details :: " + str(lockfileerr))

        if harness_lock == False:
            AbnormalTermination("Unable to find harness_lock path in host configuration file ", nverror.NvError_ResourceError)
