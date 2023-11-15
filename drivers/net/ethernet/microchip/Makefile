# SPDX-License-Identifier: GPL-2.0-only
#
# Makefile for the Microchip network device drivers.
#

ifeq ($(VERSION), 5)
ifeq ($(PATCHLEVEL), 15)
    obj-m += lan743x.o
    #include the source files from the kernel directory, for easier packaging
    lan743x-objs += 5.15/lan743x_main.o 5.15/lan743x_ethtool.o 5.15/lan743x_ptp.o
endif #PATCHLEVEL

else ifeq ($(VERSION), 6)
ifeq ($(PATCHLEVEL), 1)
    obj-m += lan743x.o
    lan743x-objs += 6.1/lan743x_main.o 6.1/lan743x_ethtool.o 6.1/lan743x_ptp.o
else ifeq ($(PATCHLEVEL), 6)
    obj-m += lan743x.o
    lan743x-objs += 6.6/lan743x_main.o 6.6/lan743x_ethtool.o 6.6/lan743x_ptp.o
endif #PATCHLEVEL


endif #VERSION
