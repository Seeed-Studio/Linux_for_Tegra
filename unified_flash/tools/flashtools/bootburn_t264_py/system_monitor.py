#!/usr/bin/python
# Copyright (c) 2017-2019, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.

import os
import sys
#from cgi import logfile
from flash_utilities import shell_utilities


class monitor(object):
    """ System Monitor

    Monitor and track states of AutoFlash modules
    """

    # common log buffer for all modules
    # format (type, id, data)
    buf = []
    outputFile = "log.txt"
    debuging = False
    logfile = None
    shellUtilities = shell_utilities()
    identifier = ""
    traceFile = None
    writeTraceOut = False
    callTraceOut = False

    def __init__(self, identifier="bootburnLog", path=os.getcwd(), debug=False):
        self.debuging = debug
        self.identifier = identifier
        self.logfile = os.path.join(path, identifier + ".txt")
        self.traceFile = path + "/" + "TRACE_" + identifier + ".txt"
        self.calltraceFile = path + "/" + "log_trace_" + identifier + ".txt"
        self.shellUtilities.removeFile(self.logfile)

    def log(self, msg, verboseLog=False):
        msg = str(msg)
        msg = "[" + self.identifier + "]: " + msg
        try:
            with open(self.logfile, "a+", 0o666) as log:
                log.write(msg + "\n")
                log.flush()
        except Exception as e:
            print("unable to write to log -- " + msg)
            print(str(e))

        if(self.debuging or verboseLog):
            print(msg)
            sys.stdout.flush()

    def vlog(self, msg):
        self.log(msg, True)

    def tag(self, msg):
        monitor.buf.append({"type":"tag", "id":self.identifier, "data":msg})

    def dbg(self, msg):
        msg = "[D]: " + msg
        self.log(msg)

    def err(self, msg):
        msg = "[E]: " + msg
        self.log(msg)

    def dump(self):
        """dump logs to console
        """
        print("dump not implemented yet")

    def trace(self, msg):
        if(self.writeTraceOut is False):
            return

        msg = "[" + self.identifier + "]: " + msg
        try:
            with open(self.traceFile, "a+", 0o666) as log:
                log.write(msg + "\n")
                log.flush()
        except Exception as e:
            print("unable to write to log -- " + msg)
            print(str(e))

        if(self.debuging):
            print(msg)
            sys.stdout.flush()

    def calltracelog(self, msg):
        if(self.callTraceOut is False):
            return

        msg = "[" + self.identifier + "]: " + msg
        try:
            with open(self.calltraceFile, "a+", 0o666) as log:
                log.write(msg + "\n")
                log.flush()
        except Exception as e:
            print("unable to write call trace to log -- " + msg)
            print(str(e))
