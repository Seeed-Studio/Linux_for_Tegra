# SPDX-License-Identifier: GPL-2.0

CONFIG_BTUSB_AUTOSUSPEND = n
CONFIG_BTUSB_WAKEUP_HOST = n
CONFIG_BTCOEX = y

ifeq ($(CONFIG_BTUSB_AUTOSUSPEND), y)
	EXTRA_CFLAGS += -DCONFIG_BTUSB_AUTOSUSPEND
endif

ifeq ($(CONFIG_BTUSB_WAKEUP_HOST), y)
	EXTRA_CFLAGS += -DCONFIG_BTUSB_WAKEUP_HOST
endif

ifeq ($(CONFIG_BTCOEX), y)
	EXTRA_CFLAGS += -DCONFIG_BTCOEX
endif

ifndef CONFIG_TEGRA_SYSTEM_TYPE_ACK
	obj-m := rtk_btusb.o
	rtk_btusb-objs := rtk_coex.o \
			rtk_misc.o \
			rtk_bt.o
endif
