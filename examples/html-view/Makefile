# TikuOS SDK example.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
SDK ?= ../../build/sdk
include $(SDK)/sdk.mk
CC ?= cc
CFLAGS ?= -O2 -g
KITS := $(foreach kit,$(SDK_KIT_NAMES),$(SDK_ROOT)/lib/libtiku_$(kit).a)
ifeq ($(SDK_SYSTEM),Darwin)
MODULE_FLAGS := -dynamiclib
else
MODULE_FLAGS := -shared
endif
.PHONY: all smoke clean
all: build/htmlview build/htmlview.so
build:
	mkdir -p $@
build/htmlview: htmlview.c | build
	$(CC) $(CFLAGS) $(SDK_CFLAGS) -o $@ $< $(KITS) $(SDK_LDFLAGS)
build/htmlview.so: htmlview.c | build
	$(CC) $(CFLAGS) $(SDK_CFLAGS) -fPIC $(MODULE_FLAGS) -DTIKU_APP_SO -o $@ $< $(KITS) $(SDK_LDFLAGS)
smoke: build/htmlview
	./build/htmlview --smoke build/preview.png
clean:
	rm -rf build
