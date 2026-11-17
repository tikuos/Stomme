# Portable model and frozen-font compilation with explicit target identity.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
FOUNDATION_TARGET ?= host-test
BUILD ?= build/portable-ui-$(FOUNDATION_TARGET)
PYTHON ?= python3
CFLAGS ?= -O2 -g
DOCUMENT_PROVIDER ?= none
ifneq ($(DOCUMENT_PROVIDER),none)
$(error portable-ui does not provide DOCUMENT_PROVIDER=$(DOCUMENT_PROVIDER))
endif
ifneq ($(strip $(TIKU_HOST)),)
$(error portable-ui uses FOUNDATION_TARGET, not TIKU_HOST)
endif
ifeq ($(FOUNDATION_TARGET),s31)
ifeq ($(strip $(CROSS_COMPILE)),)
$(error s31 requires CROSS_COMPILE)
endif
CC := $(CROSS_COMPILE)gcc
AR := $(CROSS_COMPILE)ar
NM := $(CROSS_COMPILE)nm
TARGET_FLAGS := -march=rv32imafc_zicsr_zifencei -mabi=ilp32f
else ifeq ($(FOUNDATION_TARGET),host-test)
NM ?= nm
else
$(error Unsupported FOUNDATION_TARGET=$(FOUNDATION_TARGET))
endif
PORTABLE_INCLUDES := -Ikits/support -Ikits/storage -Ikits/application/portable -Ikits/storage/portable -Ipackages/apps/models -Ipackages/apps/apps/developer-calculator
PORTABLE_FLAGS := $(TARGET_FLAGS) $(if $(findstring -flto,$(CFLAGS)),-ffat-lto-objects,) -ffunction-sections -fdata-sections -std=c11 -Wall -Wextra -Werror -Wno-unused-parameter $(CPPFLAGS) $(CFLAGS)
PORTABLE_CONFIG := $(shell $(PYTHON) tools/portable_profile.py --output '$(BUILD)' --target '$(FOUNDATION_TARGET)' --cc '$(CC)' --flags '$(PORTABLE_FLAGS)' --tools '$(AR) $(NM)')
ifeq ($(strip $(PORTABLE_CONFIG)),)
$(error Portable configuration failed)
endif
include $(PORTABLE_CONFIG)
PORTABLE_FLAGS += -include $(BUILD)/portable_build_identity.h
PORTABLE_OBJECTS := $(patsubst %.c,$(BUILD)/objects/%.o,$(PORTABLE_SOURCES)) $(BUILD)/identity.o
RENDER_OBJECTS := $(patsubst %.c,$(BUILD)/render/%.o,$(PORTABLE_RENDER_SOURCES))
RENDER_LIBRARY := $(BUILD)/lib/libtiku_render.a
FONT_OBJECTS := $(BUILD)/fonts/tiku_font_subset.o $(BUILD)/fonts/tiku_font_subset_data.o
PORTABLE_LIBRARIES := $(BUILD)/lib/libtiku_models.a $(BUILD)/lib/libtiku_font_subset.a
.DEFAULT_GOAL := all
.PHONY: all kits fonts check test test-tools clean
all: kits check
kits: $(PORTABLE_LIBRARIES) $(RENDER_LIBRARY)
fonts: $(BUILD)/generated/tiku_font_subset_data.c

$(BUILD)/identity.o: $(BUILD)/portable_identity.c $(BUILD)/configuration.stamp mk/portable.mk
	$(CC) $(PORTABLE_FLAGS) -c $< -o $@
$(BUILD)/render/%.o: %.c $(BUILD)/configuration.stamp mk/portable.mk
	@mkdir -p $(dir $@)
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) -DTIKU_PORTABLE_UI=1 -Ikits/interface -Ikits/interface/portable -Ikits/support/portable -MMD -MP -c $< -o $@
$(RENDER_LIBRARY): $(RENDER_OBJECTS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^
$(BUILD)/objects/%.o: %.c $(BUILD)/configuration.stamp mk/portable.mk
	@mkdir -p $(dir $@)
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) -DTIKU_PORTABLE_UI=1 -DTIKU_DISPLAYLESS=1 -Ikits/interface -Ikits/interface/portable -Ikits/support/portable -MMD -MP -c $< -o $@
$(BUILD)/generated/tiku_font_subset_data.c: tools/subset_fonts.py profiles/portable-fonts.json kits/interface/tiku_font_data.h
	$(PYTHON) tools/subset_fonts.py --manifest profiles/portable-fonts.json --output $(BUILD)/generated
$(BUILD)/fonts/tiku_font_subset_data.o: $(BUILD)/generated/tiku_font_subset_data.c $(BUILD)/configuration.stamp mk/portable.mk
	@mkdir -p $(dir $@)
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) -Ikits/interface -Ikits/interface/portable -MMD -MP -c $< -o $@
$(BUILD)/fonts/tiku_font_subset.o: kits/interface/portable/tiku_font_subset.c $(BUILD)/configuration.stamp mk/portable.mk
	@mkdir -p $(dir $@)
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) -Ikits/interface -Ikits/interface/portable -MMD -MP -c $< -o $@
$(BUILD)/lib/libtiku_models.a: $(PORTABLE_OBJECTS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^
$(BUILD)/lib/libtiku_font_subset.a: $(FONT_OBJECTS)
	@mkdir -p $(dir $@)
	rm -f $@
	$(AR) rcs $@ $^
check: kits
	$(PYTHON) tools/check_portable.py --nm $(NM) --profile $(BUILD)/profile.json --objects $(PORTABLE_OBJECTS) $(FONT_OBJECTS) $(RENDER_OBJECTS) --output $(BUILD)/dependencies.json
ifeq ($(FOUNDATION_TARGET),host-test)
APPLICATION_SOURCES := examples/portable-browser/browser.c examples/portable-calculator/calculator.c platform/host-test/tiku_test_pool.c
APPLICATION_FLAGS := -Ikits/interface -Ikits/interface/portable -Ikits/support/portable -Iplatform/host-test -Iexamples/portable-browser -Iexamples/portable-calculator
$(BUILD)/applications: tests/portable/applications.c tests/portable/metrics.c tests/portable/heap_trap.c $(APPLICATION_SOURCES) $(PORTABLE_LIBRARIES)
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) $(APPLICATION_FLAGS) -DTIKU_SERVICE_HEAP_TRAP=1 $< tests/portable/metrics.c tests/portable/heap_trap.c $(APPLICATION_SOURCES) -Wl,--whole-archive $(BUILD)/lib/libtiku_models.a -Wl,--no-whole-archive -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup,-Map,$@.map -o $@
.PHONY: check-applications
check-applications: $(BUILD)/applications
	$(BUILD)/applications
	$(PYTHON) tools/check_portable.py --nm $(NM) --profile $(BUILD)/profile.json --linked $(BUILD)/applications --output $(BUILD)/application-dependencies.json
test: check-applications
PANEL_FLAGS := -DTIKU_PORTABLE_UI=1 -Ikits/interface -Ikits/interface/portable -Ikits/support/portable -Iplatform/host-test -Iexamples/portable-panel
PANEL_PROVIDERS := examples/portable-panel/panel.c platform/host-test/tiku_test_pool.c platform/host-test/tiku_memory_presenter.c
PANEL_LIBS := -Wl,--whole-archive $(PORTABLE_LIBRARIES) $(RENDER_LIBRARY) -Wl,--no-whole-archive
$(BUILD)/rendering: tests/portable/rendering.c $(PANEL_PROVIDERS) tests/portable/heap_trap.c $(PORTABLE_LIBRARIES) $(RENDER_LIBRARY)
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) $(PANEL_FLAGS) -DTIKU_SERVICE_HEAP_TRAP=1 $< $(PANEL_PROVIDERS) tests/portable/heap_trap.c $(PANEL_LIBS) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup,-Map,$@.map -o $@
$(BUILD)/painting: tests/portable/painting.c $(PORTABLE_LIBRARIES) $(RENDER_LIBRARY)
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) $(PANEL_FLAGS) $< $(PANEL_LIBS) -o $@
$(BUILD)/portable-panel: examples/portable-panel/main.c $(PANEL_PROVIDERS) $(PORTABLE_LIBRARIES) $(RENDER_LIBRARY)
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) $(PANEL_FLAGS) $< $(PANEL_PROVIDERS) $(PANEL_LIBS) -o $@
.PHONY: portable-panel check-rendering
portable-panel: $(BUILD)/portable-panel
check-rendering: $(BUILD)/rendering $(BUILD)/painting
	$(BUILD)/rendering
	$(PYTHON) tools/check_pixels.py --fixture tests/portable/painting.json --program $(BUILD)/painting
	$(PYTHON) tools/check_portable.py --nm $(NM) --profile $(BUILD)/profile.json --linked $(BUILD)/rendering --output $(BUILD)/render-dependencies.json
test: check-rendering
$(BUILD)/probe: tests/portable/models.c tests/portable/metrics.c $(PORTABLE_LIBRARIES) $(BUILD)/configuration.stamp
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) -Ikits/interface -Ikits/interface/portable $< tests/portable/metrics.c -Wl,--whole-archive $(PORTABLE_LIBRARIES) -Wl,--no-whole-archive -Wl,-Map,$@.map -o $@
$(BUILD)/services: tests/portable/services.c tests/portable/metrics.c tests/portable/heap_trap.c platform/host-test/tiku_test_pool.c $(PORTABLE_LIBRARIES) $(BUILD)/configuration.stamp
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) -Ikits/interface -Ikits/interface/portable -Ikits/support/portable -Iplatform/host-test tests/portable/services.c tests/portable/metrics.c tests/portable/heap_trap.c platform/host-test/tiku_test_pool.c -DTIKU_SERVICE_HEAP_TRAP=1 -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=free,--wrap=strdup -Wl,--whole-archive $(BUILD)/lib/libtiku_models.a -Wl,--no-whole-archive -Wl,-Map,$@.map -o $@
test: check $(BUILD)/probe $(BUILD)/services
	$(BUILD)/probe
	$(BUILD)/services
	$(PYTHON) tools/check_portable.py --nm $(NM) --profile $(BUILD)/profile.json --linked $(BUILD)/services --output $(BUILD)/services-dependencies.json
	$(PYTHON) tools/check_portable.py --nm $(NM) --profile $(BUILD)/profile.json --linked $(BUILD)/probe --output $(BUILD)/link-dependencies.json
else
$(BUILD)/services.elf: tests/portable/services.c tests/portable/freestanding_memory.c platform/host-test/tiku_test_pool.c $(PORTABLE_LIBRARIES) $(BUILD)/configuration.stamp
	$(CC) $(PORTABLE_FLAGS) $(PORTABLE_INCLUDES) -ffreestanding -fno-builtin -nostdlib -static -DTIKU_SERVICES_LINK_ONLY=1 -Ikits/interface -Ikits/interface/portable -Ikits/support/portable -Iplatform/host-test tests/portable/services.c tests/portable/freestanding_memory.c platform/host-test/tiku_test_pool.c $(BUILD)/objects/kits/support/portable/tiku_allocator.o $(BUILD)/objects/kits/support/portable/tiku_clock.o $(BUILD)/objects/kits/interface/portable/tiku_event_queue.o -Wl,--no-undefined,-u,memcpy,-u,memset,-e,tiku_services_run_tests,-Map,$@.map -o $@
.PHONY: check-services
check: check-services
check-services: $(BUILD)/services.elf
	$(PYTHON) tools/check_portable.py --nm $(NM) --profile $(BUILD)/profile.json --linked $(BUILD)/services.elf --output $(BUILD)/services-dependencies.json
test:
	$(error Run host tests with FOUNDATION_TARGET=host-test; s31 is build-only)
endif
test-tools:
	$(PYTHON) -m unittest discover -s tests -p 'test_portable*.py'
clean:
	$(PYTHON) tools/build_config.py --output $(BUILD) --clean
-include $(foreach source,$(PORTABLE_SOURCES),$(BUILD)/objects/$(source:.c=.d)) $(wildcard $(BUILD)/fonts/*.d)

-include $(foreach source,$(PORTABLE_RENDER_SOURCES),$(BUILD)/render/$(source:.c=.d))

SDK_DIR ?= $(BUILD)/sdk
.PHONY: sdk check-sdk
sdk: check
	$(PYTHON) tools/export_portable_sdk.py --build $(BUILD) --output $(SDK_DIR) --ar $(AR) --nm $(NM)
check-sdk: sdk
	$(PYTHON) tests/integration/run_portable_sdk.py --sdk $(SDK_DIR) --cc $(CC) --nm $(NM) --output $(BUILD)/sdk-consumer
