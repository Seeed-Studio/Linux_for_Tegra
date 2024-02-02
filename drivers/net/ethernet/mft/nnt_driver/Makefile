KVERSION ?= $(shell uname -r)
CPU_ARCH ?= $(shell uname -m)

# Oracle Linux OS.
ifneq ($(shell if (echo $(KVERSION) | grep -qE 'uek'); then \
                                       echo "YES"; else echo ""; fi),)
override WITH_MAKE_PARAMS += ctf-dir=$(CWD)/.ctf
endif

PACKAGE_NAME = nnt-driver
PACKAGE_VERSION = 1.0.0
PACKAGE_RC = 1

%: %.in
	sed \
		-e 's/@PACKAGE_NAME@/$(PACKAGE_NAME)/g' \
		-e 's/@PACKAGE_VERSION@/$(PACKAGE_VERSION)/g' \
		-e 's/@PACKAGE_RC@/$(PACKAGE_RC)/g' \
	<$< >$@

ifneq ($(findstring ppc64, $(CPU_ARCH)),)
obj-m += mst_ppc_pci_reset.o
endif

obj-m += nnt_driver.o
nnt_driver-objs += nnt_device.o nnt_dma.o nnt_pci_conf_access.o \
		 		   nnt_pci_conf_access_no_vsec.o nnt_memory_access.o \
		   	       nnt_ioctl.o

all:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) CONFIG_CTF= CONFIG_CC_STACKPROTECTOR_STRONG= $(WITH_MAKE_PARAMS) modules

clean:
	make -C /lib/modules/$(KVERSION)/build M=$(PWD) clean
