.SUFFIXES:
.DELETE_ON_ERROR:

ifeq ($(strip $(DEVKITPRO)),)
$(error "Please set DEVKITPRO in your environment. export DEVKITPRO=<path to>/devkitpro")
endif

TOPDIR ?= $(CURDIR)

include $(DEVKITPRO)/wut/share/wut_rules

TARGET     := wwhd_tools
RPLLOADER  ?= external/wii-u-rpl-loader
LIBWUPATCH ?= $(RPLLOADER)/external/libwupatch
CRT        := $(RPLLOADER)/crt

VERSION ?=
VERSION := $(strip $(VERSION))
ifeq ($(VERSION),)
VERSION := $(shell git -C "$(TOPDIR)" rev-parse --short HEAD 2>/dev/null)
endif
ifeq ($(strip $(VERSION)),)
VERSION := dev
endif

DEBUG ?= 0
ifeq ($(DEBUG),1)
BUILD         := build_debug
OPTFLAGS      := -O0
DEBUG_DEFINES := -DWWHD_TOOLS_DEBUG=1
else
BUILD         := build
OPTFLAGS      := -O2
DEBUG_DEFINES :=
endif

DATA     := data
SOURCES  := src/app src/core src/render src/ui src/ui/panels src/hud src/tools src/cheats \
            external/imgui external/cjson $(LIBWUPATCH)/src $(CRT)
INCLUDES := include $(RPLLOADER)/include $(LIBWUPATCH)/include \
            external/libwwhd/include external/imgui external/imgui/backends/wiiu \
            external/cjson

CFLAGS   := -Wall $(OPTFLAGS) -ffunction-sections -fdata-sections -msdata=none \
            -fno-asynchronous-unwind-tables -fno-unwind-tables \
            $(MACHDEP) $(INCLUDE) -D__WIIU__ -D__WUT__ \
            -DWWHD_ENABLE_GAME_CALLS $(DEBUG_DEFINES)
CXXFLAGS := $(CFLAGS) -std=c++20 -fno-exceptions -fno-rtti
ASFLAGS  := -g $(MACHDEP)
LDFLAGS   = -g $(MACHDEP) -specs=$(WUT_ROOT)/share/wut.specs \
            -specs=$(TOPDIR)/$(CRT)/rplloader.specs -Wl,-Map,$(notdir $*.map) \
            -Wl,--gc-sections -Wl,-u,rpl_manifest -Wl,-u,rpl_cemu_entry

LIBS    := -lwut -lm
LIBDIRS := $(PORTLIBS) $(WUT_ROOT)

ifneq ($(BUILD),$(notdir $(CURDIR)))

export OUTPUT  := $(CURDIR)/$(TARGET)
export TOPDIR  := $(CURDIR)
export VPATH   := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir)) \
                  $(foreach dir,$(DATA),$(CURDIR)/$(dir))
export DEPSDIR := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CPPFILES := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES := $(foreach dir,$(DATA),$(notdir $(wildcard $(dir)/*.*)))

CPPFILES := $(filter-out imgui_demo.cpp,$(CPPFILES))
ifneq ($(DEBUG),1)
CPPFILES := $(filter-out panel_diagnostics.cpp,$(CPPFILES))
endif

ifeq ($(strip $(CPPFILES)),)
export LD := $(CC)
else
export LD := $(CXX)
endif

export OFILES_BIN := $(addsuffix .o,$(BINFILES))
export OFILES_SRC := $(CPPFILES:.cpp=.o) $(CFILES:.c=.o) $(SFILES:.s=.o)
export OFILES     := $(OFILES_BIN) $(OFILES_SRC) exports.o
export HFILES_BIN := $(addsuffix .h,$(subst .,_,$(BINFILES)))

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

.PHONY: $(BUILD) clean all

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@if [ "$$(cat .lastconfig 2>/dev/null)" != "$(BUILD)" ]; then rm -f $(TARGET).rpl $(TARGET).elf $(TARGET).map $(TARGET).lst; echo "$(BUILD)" > .lastconfig; fi
	@printf '#pragma once\n#define WWHD_TOOLS_VERSION_BASE "$(VERSION)"\n' > $@/wwhd_version.h.new
	@cmp -s $@/wwhd_version.h.new $@/wwhd_version.h || mv -f $@/wwhd_version.h.new $@/wwhd_version.h
	@rm -f $@/wwhd_version.h.new
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr build build_debug .lastconfig $(TARGET).rpl $(TARGET).elf $(TARGET).lst $(TARGET).map

WIIU_IP  ?=
TITLE_ID ?= 0005000010143500
FTP_PORT ?= 21
NAME     ?= $(TARGET).rpl

.PHONY: deploy
deploy: $(BUILD)
	@if [ -z "$(WIIU_IP)" ]; then echo "deploy: set WIIU_IP=<console ip>"; exit 1; fi
	@echo deploying $(TARGET).rpl to sd:/wiiu/rpl-loader/universal/$(NAME)
	@curl --silent --show-error --ftp-create-dirs -T $(TARGET).rpl \
		"ftp://$(WIIU_IP):$(FTP_PORT)/fs/vol/external01/wiiu/rpl-loader/universal/$(NAME)"
	@echo done

.PHONY: deploy-debug
deploy-debug:
	@$(MAKE) --no-print-directory DEBUG=1 deploy

else
.PHONY: all

DEPENDS := $(OFILES:.o=.d)

all: $(OUTPUT).rpl

PYTHON ?= $(firstword $(foreach p,python3 python py,$(shell $(p) -c "" >/dev/null 2>&1 && echo $(p))))
ifeq ($(strip $(PYTHON)),)
$(error no working Python found (tried python3, python, py); set PYTHON=<path>)
endif

$(OUTPUT).rpl: $(OUTPUT).elf
	@elf2rpl --rpl $< $@ $(ERROR_FILTER)
	@$(PYTHON) $(TOPDIR)/$(RPLLOADER)/tools/rplname.py $@ $(notdir $@)
	@echo built ... $(notdir $@)

$(OUTPUT).elf: $(OFILES)

exports.s: $(TOPDIR)/exports.def
	@echo exports ... $(notdir $<)
	@rplexportgen $< $@

exports.o: exports.s

%.bin.o %_bin.h: %.bin
	@echo $(notdir $<)
	@$(bin2o)

$(OFILES_SRC): $(HFILES_BIN)

-include $(DEPENDS)

endif
