KPVER ?= $(shell uname -r)
KSRC ?= /lib/modules/$(KPVER)/build

obj-m += bf3_livefish.o

all:
	make -C $(KSRC) M=$(PWD) CONFIG_CTF= CONFIG_CC_STACKPROTECTOR_STRONG= $(WITH_MAKE_PARAMS) modules

clean:
	make -C $(KSRC) M=$(PWD) clean

