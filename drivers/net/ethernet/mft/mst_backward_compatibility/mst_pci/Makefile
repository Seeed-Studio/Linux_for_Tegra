KPVER ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KPVER)/build

# Oracle Linux OS.
ifneq ($(shell if (echo $(KPVER) | grep -qE 'uek'); then \
                                       echo "YES"; else echo ""; fi),)
override WITH_MAKE_PARAMS += ctf-dir=$(CWD)/.ctf
endif

NNT_DRIVER_LOCATION = ../../nnt_driver
PACKAGE_NAME = nnt-driver
PACKAGE_VERSION = 1.0.0
PACKAGE_RC = 1

%: %.in
	sed \
		-e 's/@PACKAGE_NAME@/$(PACKAGE_NAME)/g' \
		-e 's/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/g' \
		-e 's/@PACKAGE_RC@/$(PACKAGE_RC)/g' \
	<$< >$@

obj-m += mst_pci.o
EXTRA_CFLAGS= -I$(PWD)/$(NNT_DRIVER_LOCATION)
mst_pci-objs += $(NNT_DRIVER_LOCATION)/nnt_device.o $(NNT_DRIVER_LOCATION)/nnt_dma.o $(NNT_DRIVER_LOCATION)/nnt_pci_conf_access.o \
		 		$(NNT_DRIVER_LOCATION)/nnt_pci_conf_access_no_vsec.o $(NNT_DRIVER_LOCATION)/nnt_memory_access.o \
		   		$(NNT_DRIVER_LOCATION)/nnt_ioctl.o mst_pci_bc.o

all:
	make -C $(KSRC) M=$(PWD) CONFIG_CTF= CONFIG_CC_STACKPROTECTOR_STRONG= $(WITH_MAKE_PARAMS) modules

clean:
	make -C $(KSRC) M=$(PWD) clean
