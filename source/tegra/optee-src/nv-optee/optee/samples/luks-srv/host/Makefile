# Copyright (c) 2021, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: BSD-2-Clause

# Input variables
# CROSS_COMPILE: The cross compiler.
# OPTEE_CLIENT_EXPORT: The base directory points to optee client's
#		       header files and libraries
# O: The base directory for build objects filetree.

CC ?= $(CROSS_COMPILE)gcc
LD ?= $(CROSS_COMPILE)ld
STRIP ?= $(CROSS_COMPILE)strip

CFLAGS += -Wall -I../ta/include -I./include
CFLAGS += -I$(OPTEE_CLIENT_EXPORT)/include
CFLAGS += -fstack-protector-strong
LDADD += -pthread -lpthread
LDADD += -lteec -L$(OPTEE_CLIENT_EXPORT)/lib
LDFLAGS := -static

SRCS = luks_srv_ca.c
OBJS = $(patsubst %.c,$(O)/%.o,$(SRCS))
BINARY = nvluks-srv-app

.PHONY: all install
all: $(BINARY) install

$(BINARY): $(OBJS)
	$(CC) $(LDFLAGS) -o $(O)/$@ $< $(LDADD)
	$(STRIP) $(O)/$@

$(O)/%.o: %.c
	mkdir -p $(O)
	$(CC) $(CFLAGS) -c $< -o $@

install: $(BINARY)
	mkdir -p $(OPTEE_CLIENT_EXPORT)/sbin
	cp $(O)/$(BINARY) $(OPTEE_CLIENT_EXPORT)/sbin

.PHONY: clean
clean:
	rm -f $(OBJS) $(O)/$(BINARY) $(OPTEE_CLIENT_EXPORT)/sbin/$(BINARY)
