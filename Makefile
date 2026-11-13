#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------

TEST_GOALS := $(filter check test% sanitize% check-module-frame /tmp/seance_%,$(MAKECMDGOALS))
HOST_ONLY_GOALS := clean release release-source release-clean $(TEST_GOALS)
NEEDS_DEVKIT := $(if $(MAKECMDGOALS),$(filter-out $(HOST_ONLY_GOALS),$(MAKECMDGOALS)),all)

ifneq ($(strip $(NEEDS_DEVKIT)),)
ifeq ($(strip $(DEVKITARM)),)
$(error "Please set DEVKITARM in your environment. export DEVKITARM=<path to>devkitARM")
endif
endif

TOPDIR ?= $(CURDIR)
ifneq ($(strip $(NEEDS_DEVKIT)),)
include $(DEVKITARM)/3ds_rules
endif

#---------------------------------------------------------------------------------
TARGET      := seance
BUILD       := build
SOURCES     := platform/3ds \
               core/audio core/dsp core/engine core/synth core/ui
INCLUDES    := core platform/3ds
APP_TITLE   := seance
APP_DESCRIPTION := drawable-cv sound computer for 3ds
APP_AUTHOR  := patausx
RELEASE_VERSION := $(strip $(shell cat $(TOPDIR)/VERSION))
# NB: 3ds_rules' smdh recipe reads APP_ICON (not ICON!) - without the export
# smdhtool silently falls back to the devkitPro default icon
export APP_ICON := $(TOPDIR)/assets/icon.png
export APP_SMALL_ICON := $(TOPDIR)/assets/icon_small.png

#---------------------------------------------------------------------------------
ARCH    := -march=armv6k -mtune=mpcore -mfloat-abi=hard -mtp=soft

CFLAGS  := -g -Wall -O2 -mword-relocations -ffunction-sections $(ARCH)
CFLAGS  += $(INCLUDE) -D__3DS__
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -std=gnu++17 \
            -DSEANCE_VERSION='"$(RELEASE_VERSION)"'

ASFLAGS := -g $(ARCH)
LDFLAGS  = -specs=3dsx.specs -g $(ARCH) -Wl,-Map,$(notdir $*.map)

LIBS    := -lcitro2d -lcitro3d -lctru -lm
LIBDIRS := $(CTRULIB)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
.DEFAULT_GOAL := all
#---------------------------------------------------------------------------------
export OUTPUT   := $(CURDIR)/$(TARGET)
export TOPDIR   := $(CURDIR)
export VPATH    := $(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export DEPSDIR  := $(CURDIR)/$(BUILD)

CFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
# Retired Relic material and menu-audio sources are local history, never linked.
CPPFILES := $(filter-out relic_material.cpp menu_audio.cpp,$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp))))
SFILES   := $(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.s)))
BINFILES :=

ifeq ($(strip $(CPPFILES)),)
    export LD := $(CC)
else
    export LD := $(CXX)
endif

export CXX_OFILES := $(CPPFILES:.cpp=.o)
export OFILES := $(addsuffix .o,$(BINFILES)) \
                 $(CXX_OFILES) $(CFILES:.c=.o) $(SFILES:.s=.o)

export INCLUDE := $(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
                  $(foreach dir,$(LIBDIRS),-I$(dir)/include) \
                  -I$(CURDIR)/$(BUILD)

export LIBPATHS := $(foreach dir,$(LIBDIRS),-L$(dir)/lib)

export _3DSXFLAGS += --smdh=$(CURDIR)/$(TARGET).smdh

.PHONY: $(BUILD) clean all run send cia cci test sanitize test-branding release release-app release-source release-clean

all: $(BUILD)

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(TARGET).3dsx $(TARGET).smdh $(TARGET).elf $(TARGET).cia $(TARGET).3ds

# wifi upload to the 3ds via netloader (press Y in the homebrew launcher)
# uses $THREEDS_IP when set, otherwise 3dslink searches via broadcast
run: $(BUILD)
	3dslink $(if $(THREEDS_IP),-a $(THREEDS_IP)) $(TARGET).3dsx

send: run

# Host tests are local-only and never part of the application build.
ifneq ($(strip $(TEST_GOALS)),)
ifeq ($(wildcard $(TOPDIR)/tests/Makefile),)
$(error host checks require local tests/Makefile; tests are not installed in public source exports)
else
include $(TOPDIR)/tests/Makefile
endif
endif

release-app: $(BUILD) cia
	python3 scripts/package_release.py --main --version "$(RELEASE_VERSION)"

# Application archive only; matching source is a separate duty.
# For source-bound candidates use scripts/prepare_public_release.py explicitly.
release: release-app

release-source:
	python3 scripts/package_release.py --source --version "$(RELEASE_VERSION)"

release-clean:
	rm -f release/seance-*.zip release/*.cia release/SHA256SUMS.txt \
	  seance-tester-*.zip SHA256SUMS.txt

# CIA / CCI packaging uses makerom and devkitPro smdhtool.
MAKEROM ?= $(HOME)/tools/3ds/makerom

$(BUILD)/$(TARGET)_cia.smdh: assets/icon.png assets/icon_small.png Makefile
	@mkdir -p $(BUILD)
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" \
	  assets/icon.png $@ assets/icon_small.png

# Legacy-mode CIA/CCI: retain the existing no-banner/no-audio packaging, but
# show the same gathering icon as the homebrew launcher (Old and New 3DS).
cia: $(BUILD) $(BUILD)/$(TARGET)_cia.smdh
	$(MAKEROM) -f cia -o $(TARGET).cia -elf $(TARGET).elf -rsf $(TARGET).rsf \
	  -icon $(BUILD)/$(TARGET)_cia.smdh -target t

cci: $(BUILD) $(BUILD)/$(TARGET)_cia.smdh
	$(MAKEROM) -f cci -o $(TARGET).3ds -elf $(TARGET).elf -rsf $(TARGET).rsf \
	  -icon $(BUILD)/$(TARGET)_cia.smdh -target t

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------
DEPENDS := $(OFILES:.o=.d)

$(OUTPUT).3dsx : $(OUTPUT).elf $(OUTPUT).smdh
$(OUTPUT).smdh : $(APP_ICON) $(APP_SMALL_ICON) $(TOPDIR)/Makefile
	smdhtool --create "$(APP_TITLE)" "$(APP_DESCRIPTION)" "$(APP_AUTHOR)" \
	  $(APP_ICON) $@ $(APP_SMALL_ICON)
$(OUTPUT).elf  : $(OFILES) $(TOPDIR)/VERSION

# SEANCE_VERSION is a C++ command-line define, which make cannot track itself.
# Invalidate every C++ consumer as well as the link when VERSION changes.
ifneq ($(strip $(CXX_OFILES)),)
$(CXX_OFILES): $(TOPDIR)/VERSION
endif

# Already prepared PCM travels inside the executable; builds require neither
# ffmpeg nor access to the original uploads. bin2o exports aligned const data.
%.bin.o: %.bin
	$(bin2o)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
