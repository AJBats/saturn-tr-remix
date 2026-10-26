# Tomb Raider Saturn remaster
#
# external_resources/ (gitignored) must contain the Tomb Raider (USA) Redump
# image extracted to external_resources/tomb_raider_usa/.
#
#   make disc     $(BUILD)/disc/tomb.cue (our 0MAIN.BIN + the original audio tracks)
#   make bench    $(BUILD)/bench/disc/tomb.cue (CPU benchmark, saturn/progs/bench.cpp)
#   make cdtest   $(BUILD)/cdtest/disc/tomb.cue (CD loader self-test)
#   make ghidra   headless analysis of the original 0MAIN.BIN
#
# First run: python tools/setup_toolchain.py (pinned GCC 14.2 into toolchain/).
# Toolchain, flags and output directory can be overridden, e.g.
#   make bench SH_BIN=... SH_PREFIX=sh-elf- OPT=-O2 BUILD=build/gcc9-O2

PY      ?= python -I
GHIDRA  ?= C:/Users/albat/Documents/ghidra_12.0_PUBLIC_20251205/ghidra_12.0_PUBLIC
OPT     ?= -Os
BUILD   ?= build

# Pinned toolchain: GCC 14.2.0 sh2eb-elf, installed by tools/setup_toolchain.py.
TOOLCHAIN := toolchain/gcc-14.2.0
SH_PREFIX ?= sh2eb-elf-
ifeq ($(OS),Windows_NT)
SH_BIN  ?= $(TOOLCHAIN)/sh2eb-elf/bin
PATHSEP := ;
else
SH_BIN  ?= $(TOOLCHAIN)/linux/sh2eb-elf/bin
PATHSEP := :
endif

# Tools are found through PATH (cc1.exe also loads its DLLs from there).
export PATH := $(abspath $(SH_BIN))$(PATHSEP)$(PATH)

CC      := $(SH_PREFIX)gcc
CXX     := $(SH_PREFIX)g++
OBJCOPY := $(SH_PREFIX)objcopy
# -ffunction-sections: saturn.ld places the hot functions first, in a fixed order
CFLAGS  := -m2 -mb $(OPT) -Wall -D__SATURN__ -Isaturn -Iopenlara -I$(BUILD)/gen -ffunction-sections
ifdef START_LEVEL
CFLAGS  += -DSTART_LEVEL=$(START_LEVEL)   # testing: touch saturn/main.cpp when changing it
endif
ifdef START_YAW
CFLAGS  += -DSTART_YAW=$(START_YAW)       # testing: Lara's heading at the start; touch saturn/main.cpp
endif
ifdef HW_LOG
CFLAGS  += -DHW_LOG     # testing on hardware: the boot log, stats and measurements on screen; touch saturn/crash.cpp saturn/debug.cpp when changing it
endif
ifdef VOID_VIEW
CFLAGS  += -DVOID_VIEW  # testing: what nothing draws in the scene shows magenta; touch saturn/render.cpp
endif
# -fno-lifetime-dse: OpenLara sets ItemObj fields and then placement-news the
# subclass over them (ItemObj::init); the constructors read those fields.
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-use-cxa-atexit \
	-fno-asynchronous-unwind-tables -fno-lifetime-dse -Wno-class-memaccess
LDFLAGS := -nostdlib -T saturn/saturn.ld -Wl,--no-warn-rwx-segments

# sources compiled with -O2 in place of $(OPT) (touch them when it changes):
# the renderer's (-1 at the bench spot in Mednafen); the game's logic in
# main.cpp would overflow high work RAM
O2_SRC  ?= saturn/render.cpp saturn/rooms.cpp openlara/common.cpp
$(patsubst %,$(BUILD)/obj/%.o,$(O2_SRC)): CXXFLAGS := $(subst $(OPT),-O2,$(CXXFLAGS))

ER      := external_resources
ORIG    := $(ER)/tomb_raider_usa
TRACK1  := $(ORIG)/Tomb Raider (USA) (Track 01).bin
ORIGCUE := $(ORIG)/Tomb Raider (USA).cue
DISC    := $(ER)/disc
MAIN    := $(DISC)/files/0MAIN.BIN
GH_OUT  := build/ghidra

# The game: every platform source plus the OpenLara core.
GAME_SRC := $(wildcard saturn/*.c saturn/*.cpp saturn/*.s) openlara/common.cpp
ENGINE_SRC := $(filter-out saturn/main.cpp,$(GAME_SRC))
HW_SRC  := saturn/crt0.s saturn/irq.s saturn/system.cpp saturn/cd.cpp

# Test programs in saturn/progs/, each built onto its own disc by `make NAME`.
# NAME_DISC lists extra disc files as DISCPATH=SRCPATH.
PROGS   := bench cdtest
bench_SRC  := $(ENGINE_SRC) saturn/progs/bench.cpp
cdtest_SRC := $(HW_SRC) saturn/progs/cdtest.cpp
cdtest_DISC := DATA/LEVEL1.SAT=$(DISC)/files/DATA/LEVEL1.SAT

DISC_TXT := ABS.TXT=disc/ABS.TXT BIB.TXT=disc/BIB.TXT CPY.TXT=disc/CPY.TXT

# $(call DISC_RULE,dir,extra entries): dir/disc/tomb.cue holding dir/0MAIN.BIN
define DISC_RULE
$(1)/disc/tomb.cue: $(1)/0MAIN.BIN $(wildcard disc/*) tools/mkdisc.py $(DISC)/filelist.txt $(foreach e,$(2),$(lastword $(subst =, ,$(e))))
	$$(PY) tools/mkdisc.py --ip $(DISC)/IP.BIN --cue "$(ORIGCUE)" --out $(1)/disc --name tomb \
		0MAIN.BIN=$(1)/0MAIN.BIN $(DISC_TXT) $(2)
endef

# SCU DSP programs, assembled into headers (tools/dspasm.py): saturn/NAME.dsp
# to NAME_dsp.h holding NAME_dsp[]
DSP_HEADERS := $(patsubst saturn/%.dsp,$(BUILD)/gen/%_dsp.h,$(wildcard saturn/*.dsp))
HEADERS := $(wildcard saturn/*.h openlara/*.h openlara/*/*.h) $(DSP_HEADERS)

.PHONY: all disc $(PROGS) extract ghidra check-toolchain

# keep intermediate files (0MAIN.BIN) produced through pattern rules
.SECONDARY:

all: disc

check-toolchain:
	@test -d "$(SH_BIN)" || { echo "No SH toolchain at $(SH_BIN): run python tools/setup_toolchain.py"; exit 1; }

$(BUILD)/obj/%.c.o: %.c $(HEADERS) | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.cpp.o: %.cpp $(HEADERS) | check-toolchain
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/%.s.o: %.s | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gen/%_dsp.h: saturn/%.dsp tools/dspasm.py
	@mkdir -p $(@D)
	$(PY) tools/dspasm.py $< $@ $*_dsp

%/0MAIN.BIN: %/0MAIN.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/0MAIN.elf: $(patsubst %,$(BUILD)/obj/%.o,$(GAME_SRC)) saturn/saturn.ld
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Wl,-Map=$(@D)/0MAIN.map -o $@ $(filter %.o,$^) -lc -lgcc

# Levels packed from the original disc's DATA files (tools/pack_level.py).
LEVELS  := TITLE GYM LEVEL1 LEVEL2 LEVEL3A LEVEL3B LEVEL4 LEVEL5 LEVEL6 LEVEL7A LEVEL7B \
           LEVEL8A LEVEL8B LEVEL8C LEVEL10A LEVEL10B LEVEL10C CUT1 CUT2 CUT3 CUT4
GAME_DISC := $(foreach l,$(LEVELS),$(foreach e,PKD TEX SUB SFX ITX PAL,DATA/$(l).$(e)=build/data/$(l).$(e)))
# the title and loading pictures (saturn/screen.h)
PICTURES := USATIT USAOPT GYM224 AZTEC224 GREEK224 EGYPT224 ATLAN224 EIDOS224 CRED224 ENDPG224
GAME_DISC += $(foreach p,$(PICTURES),BINDATA/$(p).BIN=$(DISC)/files/BINDATA/$(p).BIN)
# the original's videos (saturn/fmv.h)
VIDEOS := CORELOGO CAFE MANSION SNOW LIFT VISION CANYON PYRAMID PRISON END
GAME_DISC += $(foreach v,$(VIDEOS),FMV/$(v).CPK=$(DISC)/files/FMV/$(v).CPK)

build/data/%.PKD build/data/%.TEX build/data/%.SUB build/data/%.SFX build/data/%.ITX build/data/%.PAL &: tools/pack_level.py tools/trsat.py openlara/common.h saturn/vram.h $(DISC)/filelist.txt
	$(PY) tools/pack_level.py $(DISC)/files/DATA $* build/data/$*.PKD

disc: $(BUILD)/disc/tomb.cue
$(eval $(call DISC_RULE,$(BUILD),$(GAME_DISC)))

define PROG_RULES
$(BUILD)/$(1)/0MAIN.elf: $$(patsubst %,$(BUILD)/obj/%.o,$$($(1)_SRC)) saturn/saturn.ld
	@mkdir -p $$(@D)
	$$(CXX) $$(CXXFLAGS) $$(LDFLAGS) -Wl,-Map=$$(@D)/0MAIN.map -o $$@ $$(filter %.o,$$^) -lc -lgcc

$(1): $(BUILD)/$(1)/disc/tomb.cue
$$(eval $$(call DISC_RULE,$(BUILD)/$(1),$$($(1)_DISC)))
endef
$(foreach p,$(PROGS),$(eval $(call PROG_RULES,$(p))))

extract: $(DISC)/filelist.txt

$(DISC)/filelist.txt:
	$(PY) tools/extract_disc.py "$(TRACK1)" $(DISC)

# Headless Ghidra import + analysis of the original program, discovery of
# functions reached only through pointers, then export of functions.tsv,
# decomp.c and main.s to $(GH_OUT).
ghidra: $(GH_OUT)/functions.tsv

$(GH_OUT)/functions.tsv: $(DISC)/filelist.txt $(wildcard tools/ghidra/*.java)
	mkdir -p $(GH_OUT)/project
	"$(GHIDRA)/support/analyzeHeadless.bat" $(GH_OUT)/project TombRaider \
		-import $(MAIN) -overwrite \
		-loader BinaryLoader -loader-baseAddr 0x06004000 \
		-processor SuperH:BE:32:SH-2 \
		-scriptPath tools/ghidra \
		-preScript SaturnSetup.java \
		-postScript FindFunctions.java \
		-postScript ExportAll.java $(GH_OUT) \
		-log $(GH_OUT)/analyze.log
