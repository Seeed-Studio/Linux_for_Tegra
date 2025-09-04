subdirs-y += lib

global-incdirs-y += include
global-incdirs-y += reference/include
global-incdirs-y += platform/include

srcs-y += platform/AdminPPI.c
srcs-y += platform/Cancel.c
srcs-y += platform/Clock.c
srcs-y += platform/DebugHelpers.c
srcs-y += platform/Entropy.c
srcs-y += platform/LocalityPlat.c
srcs-y += platform/NvAdmin.c
srcs-y += platform/NVMem.c
srcs-y += platform/PowerPlat.c
srcs-y += platform/PlatformData.c
srcs-y += platform/PPPlat.c
srcs-y += platform/RunCommand.c
srcs-y += platform/Unique.c
srcs-y += platform/EPS.c
srcs-y += platform/PlatformACT.c
srcs-y += reference/RuntimeSupport.c
srcs-y += platform/fTPM_helpers.c

srcs-y += fTPM.c

ifeq ($(CFG_TA_MEASURED_BOOT),y)
# Support for Trusted Firmware Measured Boot.
srcs-y += platform/fTPM_event_log.c
srcs-y += platform/EventLogPrint.c
endif
