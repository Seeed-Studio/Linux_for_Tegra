BINARY=bc50d971-d4c9-42c4-82cb-343fb7f37896

TPM_ROOT_PATH		:= ../../../../
WARNS			:= 0
NOWERROR		:= 0
CFG_TA_MEASURED_BOOT	?= n
CFG_TA_DEBUG		?= n
CFG_TEE_TA_LOG_LEVEL	?= 0
CFG_TA_EVENT_LOG_SIZE	?= 1024
CFG_USE_PLATFORM_EPS	?= n
TA_CPU			?= cortex-a57+fp+simd+crypto+crc

cflags-y +=	-DTHIRTY_TWO_BIT	\
		-D_ARM_			\
		-w			\
		-Wno-strict-prototypes	\
		-mcpu=$(TA_CPU)		\
		-Wstack-protector

ifeq ($(CFG_TA_MEASURED_BOOT),y)
cflags-y += -DEVENT_LOG_SIZE=$(CFG_TA_EVENT_LOG_SIZE)
cflags-y += -DMEASURED_BOOT
endif

ifeq ($(CFG_TA_DEBUG),y)
cflags-y += -DfTPMDebug=1
cflags-y += -DDBG=1
cflags-y += -DDEBUG
endif

include $(TA_DEV_KIT_DIR)/mk/ta_dev_kit.mk

ifeq ($(CFG_ARM64_ta_arm64),y)
cflags-y += -mstrict-align
else
cflags-y += -mno-unaligned-access
endif
