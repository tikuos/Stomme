# Standalone shared kit libraries and relocatable SDK.
# Authors: Ambuj Varshney <ambuj@tiku-os.org>
# SPDX-License-Identifier: Apache-2.0
BUILD ?= build
CC ?= cc
AR ?= ar
CFLAGS ?= -O2 -g
CPPFLAGS += -D_DEFAULT_SOURCE
CFLAGS += -std=gnu11 -Wall -Wextra -Werror=implicit-function-declaration -Werror=implicit-int
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
TIKU_HOST ?= cocoa
else
TIKU_HOST ?= x11
endif
KITS := support device interface storage translation application media editor
DOCUMENT_PROVIDER ?= none
ifneq ($(filter-out none tiku-basic,$(DOCUMENT_PROVIDER)),)
$(error Unsupported DOCUMENT_PROVIDER=$(DOCUMENT_PROVIDER))
endif
LINK_ORDER := editor application translation interface storage device media support
ifeq ($(DOCUMENT_PROVIDER),tiku-basic)
KITS += document_formats document
LINK_ORDER := editor application document_formats translation document interface storage device media support
DOCUMENT := $(wildcard packages/document-basic/kits/document/*.c)
DOCUMENT_FORMATS := $(wildcard packages/document-basic/kits/translation/*.c)
endif
INCLUDES := $(addprefix -Ikits/,$(KITS)) -I$(BUILD)/generated
ifeq ($(DOCUMENT_PROVIDER),tiku-basic)
INCLUDES += -Ipackages/document-basic/kits/document -Ipackages/document-basic/kits/translation
endif
SDK_DIR ?= $(BUILD)/sdk
STRINGS_INPUT ?= resources/strings/en.xml
SCORES_DIR ?= resources/sounds

SUPPORT := $(filter-out %/tiku_sys_linux.c %/tiku_sys_darwin.c,$(wildcard kits/support/*.c))
DEVICE := $(wildcard kits/device/*.c)
INTERFACE := $(filter-out %/tiku_x11.c %/tiku_x11_clipboard.c %/tiku_x11_command_linux.c %/tiku_x11_command_darwin.c %/tiku_access_linux.c %/tiku_access_linux_methods.c %/tiku_access_linux_text.c %/tiku_access_linux_table.c %/tiku_print_linux.c,$(wildcard kits/interface/*.c))
STORAGE := $(wildcard kits/storage/*.c)
TRANSLATION := $(wildcard kits/translation/*.c)
APPLICATION := $(wildcard kits/application/*.c)
MEDIA := $(filter-out %/tiku_sound_coreaudio.c %/tiku_sound_pulse.c,$(wildcard kits/media/*.c))
EDITOR := $(wildcard kits/editor/*.c)

ifeq ($(UNAME_S),Darwin)
SUPPORT += kits/support/tiku_sys_darwin.c
MEDIA += kits/media/tiku_sound_coreaudio.c
CPPFLAGS += -D_DARWIN_C_SOURCE -include kits/support/tiku_darwin.h
LDLIBS += -framework AudioToolbox -framework DiskArbitration -framework CoreFoundation -framework Cocoa -framework PDFKit
INTERFACE += kits/interface/tiku_access_cocoa.m kits/interface/tiku_print_cocoa.m
else
SUPPORT += kits/support/tiku_sys_linux.c
MEDIA += kits/media/tiku_sound_pulse.c
INTERFACE += kits/interface/tiku_access_linux.c kits/interface/tiku_access_linux_methods.c kits/interface/tiku_access_linux_text.c kits/interface/tiku_access_linux_table.c kits/interface/tiku_print_linux.c
GIO_CFLAGS := $(shell pkg-config --cflags gio-2.0 gio-unix-2.0)
GIO_LIBS := $(shell pkg-config --libs gio-2.0 gio-unix-2.0)
CPPFLAGS += $(GIO_CFLAGS)
LDLIBS += $(GIO_LIBS) -ldl
endif
ifeq ($(TIKU_HOST),cocoa)
ifneq ($(UNAME_S),Darwin)
$(error Cocoa requires Darwin)
endif
INTERFACE += kits/interface/tiku_cocoa.m
LDLIBS += -framework Cocoa -framework QuartzCore
else ifeq ($(TIKU_HOST),x11)
INTERFACE += kits/interface/tiku_x11.c kits/interface/tiku_x11_clipboard.c
ifeq ($(UNAME_S),Darwin)
X11_PREFIX ?= /opt/X11
CPPFLAGS += -I$(X11_PREFIX)/include
LDFLAGS += -L$(X11_PREFIX)/lib
INTERFACE += kits/interface/tiku_x11_command_darwin.c
else
INTERFACE += kits/interface/tiku_x11_command_linux.c
endif
LDLIBS += -lX11
else
$(error Unsupported TIKU_HOST=$(TIKU_HOST))
endif
LDLIBS += -lm -pthread

SOURCES := $(sort $(foreach k,$(KITS),$($(shell echo $(k) | tr a-z A-Z))))
SDK_IDENTITY := $(shell python3 tools/standalone_identity.py --output "$(BUILD)/generated" --provider "$(DOCUMENT_PROVIDER)" --host "$(TIKU_HOST)" --flags '$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(LDLIBS)')
ifeq ($(strip $(SDK_IDENTITY)),)
$(error SDK identity generation failed)
endif
CPPFLAGS += -include $(BUILD)/generated/tiku_sdk_identity.h
CONFIG_STAMP := $(shell python3 tools/build_config.py --output "$(BUILD)" --host "$(TIKU_HOST)" --flags '$(CC) $(CPPFLAGS) $(CFLAGS) $(LDFLAGS) $(LDLIBS) $(STRINGS_INPUT) $(SCORES_DIR) $(DOCUMENT_PROVIDER) $(SDK_IDENTITY)')
ifeq ($(strip $(CONFIG_STAMP)),)
$(error Build configuration failed)
endif
OBJECTS := $(addprefix $(BUILD)/obj/,$(addsuffix .o,$(basename $(SOURCES))))
PIC_OBJECTS := $(addprefix $(BUILD)/obj/pic/,$(addsuffix .o,$(basename $(SOURCES))))
LIBRARIES := $(foreach k,$(LINK_ORDER),$(BUILD)/lib/libtiku_$(k).a)
PIC_LIBRARIES := $(foreach k,$(LINK_ORDER),$(BUILD)/lib/pic/libtiku_$(k).a)
.DEFAULT_GOAL := all
.PHONY: all kits sdk clean prepare-build check check-architecture check-font lint install-sdk
all: kits sdk
kits: $(LIBRARIES) $(PIC_LIBRARIES)
prepare-build:
	@:
$(OBJECTS) $(PIC_OBJECTS): $(CONFIG_STAMP) Makefile

$(BUILD)/obj/%.o: %.c | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -Wno-unused-parameter -MMD -MP -c -o $@ $<
$(BUILD)/obj/%.o: %.m | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -Wno-unused-parameter -MMD -MP -c -o $@ $<
$(BUILD)/obj/pic/%.o: %.c | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -Wno-unused-parameter -fPIC -MMD -MP -c -o $@ $<
$(BUILD)/obj/pic/%.o: %.m | prepare-build
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -Wno-unused-parameter -fPIC -MMD -MP -c -o $@ $<

define ARCHIVE
$(BUILD)/lib/libtiku_$(1).a: $$(addprefix $(BUILD)/obj/,$$(addsuffix .o,$$(basename $($(2)))))
	@mkdir -p $$(dir $$@)
	rm -f $$@
	$$(AR) rcs $$@ $$^
$(BUILD)/lib/pic/libtiku_$(1).a: $$(addprefix $(BUILD)/obj/pic/,$$(addsuffix .o,$$(basename $($(2)))))
	@mkdir -p $$(dir $$@)
	rm -f $$@
	$$(AR) rcs $$@ $$^
endef
$(foreach k,$(KITS),$(eval $(call ARCHIVE,$(k),$(shell echo $(k) | tr a-z A-Z))))

$(BUILD)/generated/tiku_strings_default.h: $(STRINGS_INPUT) kits/support/tools/genstrings.py | prepare-build
	@mkdir -p $(dir $@)
	python3 kits/support/tools/genstrings.py $< > $@.tmp && mv $@.tmp $@
$(BUILD)/generated/tiku_saslprep_tables.h: kits/support/tools/gensaslprep.py | prepare-build
	@mkdir -p $(dir $@)
	python3 $< > $@.tmp && mv $@.tmp $@
$(BUILD)/generated/tiku_scores_default.h: $(wildcard $(SCORES_DIR)/*.score) kits/media/tools/genscores.py | prepare-build
	@mkdir -p $(dir $@)
	python3 kits/media/tools/genscores.py $(SCORES_DIR) > $@.tmp && mv $@.tmp $@
$(BUILD)/obj/kits/support/tiku_strings.o $(BUILD)/obj/pic/kits/support/tiku_strings.o: $(BUILD)/generated/tiku_strings_default.h
$(BUILD)/obj/kits/support/tiku_saslprep.o $(BUILD)/obj/pic/kits/support/tiku_saslprep.o: $(BUILD)/generated/tiku_saslprep_tables.h
$(BUILD)/obj/kits/media/tiku_sound_scores.o $(BUILD)/obj/pic/kits/media/tiku_sound_scores.o: $(BUILD)/generated/tiku_scores_default.h

sdk: $(PIC_LIBRARIES)
	python3 tools/export_sdk.py --output "$(SDK_DIR)" --provider "$(DOCUMENT_PROVIDER)" --identity "$(BUILD)/generated" --host "$(TIKU_HOST)" --cflags '$(CPPFLAGS) $(CFLAGS)' --ldflags '$(LDFLAGS) $(LDLIBS)' --libraries $(PIC_LIBRARIES)
install-sdk: sdk
	@test -n "$(SDK_PREFIX)" || { echo 'Set SDK_PREFIX'; exit 1; }
	python3 tools/export_sdk.py --output "$(SDK_PREFIX)" --copy "$(SDK_DIR)"
check-architecture:
	python3 tools/check_dependencies.py --provider "$(DOCUMENT_PROVIDER)"
check-font:
	python3 kits/interface/tools/check_font.py
lint:
	python3 tools/check_comment_style.py --root="$(CURDIR)" --prose=6 --header=20
	python3 tools/check_comment_style.py --root="$(CURDIR)" --prose=6 --header=20 --strict kits packages hygiene
	python3 tools/check_prose_style.py
check: check-architecture check-font lint
clean:
	python3 tools/build_config.py --output "$(BUILD)" --clean

PACKAGE_TEST_SOURCES := $(if $(filter tiku-basic,$(DOCUMENT_PROVIDER)),$(wildcard packages/document-basic/tests/test_*.c))
TEST_NAMES := $(basename $(notdir $(PACKAGE_TEST_SOURCES))) $(basename $(notdir $(wildcard tests/kits/test_*.c)))
TEST_BINS := $(addprefix $(BUILD)/tests/,$(TEST_NAMES))
ifeq ($(UNAME_S),Linux)
test_remote_snapshot_FLAGS := -Wl,--wrap=realloc
test_print_rpc_FLAGS := -Wl,--wrap=tiku_remote_session_tell
endif
.PHONY: tests test displayless check-link test-tools
tests: $(TEST_BINS)
$(BUILD)/tests/%: tests/kits/%.c $(LIBRARIES)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBRARIES) $(LDFLAGS) $(LDLIBS) $($*_FLAGS)
$(BUILD)/tests/%: packages/document-basic/tests/%.c $(LIBRARIES)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBRARIES) $(LDFLAGS) $(LDLIBS)
test: tests
	@for suite in $(TEST_BINS); do echo "RUN $$suite"; $$suite || exit 1; done
DISPLAYLESS_NAMES := tiku_field tiku_scroll tiku_textview tiku_slider tiku_doc tiku_markdown tiku_field_event tiku_dialog tiku_press tiku_choice tiku_text_input tiku_sensor tiku_text_layout tiku_properties tiku_preferences_layout tiku_tabs tiku_list tiku_layout tiku_colors tiku_form_row
DISPLAYLESS_SRC := $(wildcard $(addprefix kits/interface/,$(addsuffix .c,$(DISPLAYLESS_NAMES))))
DISPLAYLESS_OBJ := $(addprefix $(BUILD)/displayless/,$(addsuffix .o,$(basename $(notdir $(DISPLAYLESS_SRC)))))
DISPLAYLESS_FLAGS := -std=gnu11 -DTIKU_DISPLAYLESS -DTIKU_DOC_CSS_RULES_MAX=0 -DTIKU_DOC_TEXT_MAX=2048 -Ikits/interface
$(BUILD)/displayless/%.o: kits/interface/%.c $(CONFIG_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(DISPLAYLESS_FLAGS) -O2 -MMD -MP -c -o $@ $<
$(BUILD)/displayless/test: tests/displayless.c $(BUILD)/displayless/tiku_doc.o
	$(CC) $(DISPLAYLESS_FLAGS) -o $@ $^
displayless: $(DISPLAYLESS_OBJ) $(BUILD)/displayless/test
	$(AR) rcs $(BUILD)/displayless/libtiku_models.a $(DISPLAYLESS_OBJ)
	$(BUILD)/displayless/test
	@! nm -u $(BUILD)/displayless/tiku_doc.o | grep -E ' (malloc|calloc|realloc|free|tiku_font_[a-z_]+)$$'
check-link: $(PIC_LIBRARIES)
ifeq ($(UNAME_S),Darwin)
	$(CC) -dynamiclib -Wl,-all_load -o $(BUILD)/all-kits.dylib $(PIC_LIBRARIES) $(LDFLAGS) $(LDLIBS)
else
	$(CC) -shared -Wl,--no-undefined -Wl,--whole-archive $(PIC_LIBRARIES) -Wl,--no-whole-archive -o $(BUILD)/all-kits.so $(LDFLAGS) $(LDLIBS)
endif
test-tools:
	python3 -m unittest discover -s tests -p 'test_*.py'
	python3 -m unittest discover -s hygiene -p 'test_*.py'
-include $(shell find $(BUILD)/obj -name '*.d' -type f 2>/dev/null)
-include $(wildcard $(BUILD)/displayless/*.d)

$(BUILD)/tools/glyphdump: kits/interface/tools/glyphdump.c $(LIBRARIES)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -o $@ $< $(LIBRARIES) $(LDFLAGS) $(LDLIBS)

.PHONY: resource-client
resource-client: $(BUILD)/stomme-resources
$(BUILD)/stomme-resources: examples/stomme_resources.c $(BUILD)/lib/libtiku_application.a $(BUILD)/lib/libtiku_support.a
	$(CC) $(CPPFLAGS) $(CFLAGS) $(INCLUDES) -o $@ $< $(BUILD)/lib/libtiku_application.a $(BUILD)/lib/libtiku_support.a $(LDFLAGS) -lm -pthread
