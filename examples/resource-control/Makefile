# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
SDK ?= ../../build/sdk
include $(SDK)/sdk.mk
CC ?= cc
CFLAGS ?= -O2 -g
.PHONY: all clean
all: build/stomme-resources
build/stomme-resources: ../stomme_resources.c
	mkdir -p $(@D)
	$(CC) $(CFLAGS) $(SDK_CFLAGS) -o $@ $< $(SDK_ROOT)/lib/libtiku_application.a $(SDK_ROOT)/lib/libtiku_support.a $(SDK_LDFLAGS)
clean:
	rm -rf build
