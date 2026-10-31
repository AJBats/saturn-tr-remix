# Tomb Raider Saturn remaster
#
# external_resources/ (gitignored) must contain the Tomb Raider (USA) Redump
# image extracted to external_resources/tomb_raider_usa/.
#
#   make disc     $(BUILD)/disc/tomb.cue (our 0MAIN.BIN + the original audio tracks)
#   make ghidra   headless analysis of the original 0MAIN.BIN
#
# First run: python tools/setup_toolchain.py (pinned GCC 14.2 into toolchain/).
# Toolchain, flags and output directory can be overridden, e.g.
#   make disc SH_BIN=... SH_PREFIX=sh-elf- OPT=-O2 BUILD=build/gcc9-O2

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
CFLAGS  := -m2 -mb $(OPT) -Wall -D__SATURN__ -Isaturn -Iopenlara -Irenderer -I$(BUILD)/gen -ffunction-sections
ifdef START_LEVEL
CFLAGS  += -DSTART_LEVEL=$(START_LEVEL)   # testing
endif
ifdef START_YAW
CFLAGS  += -DSTART_YAW=$(START_YAW)       # testing: Lara's heading at the start
endif
ifdef HW_LOG
CFLAGS  += -DHW_LOG     # testing on hardware: the boot log, stats, measurements and frame trace on screen
endif
ifdef SYNC_TEST
CFLAGS  += -DSYNC_TEST  # testing, with HW_LOG: the CPUs' synchronisation test at boot (synctest.cpp)
endif
ifdef VOID_VIEW
CFLAGS  += -DVOID_VIEW  # testing: what nothing draws in the scene shows magenta
endif
# -fno-lifetime-dse: OpenLara sets ItemObj fields and then placement-news the
# subclass over them (ItemObj::init); the constructors read those fields.
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-use-cxa-atexit \
	-fno-asynchronous-unwind-tables -fno-lifetime-dse -Wno-class-memaccess
LDFLAGS := -nostdlib -T saturn/saturn.ld -Wl,--no-warn-rwx-segments

# sources compiled with -O2 in place of $(OPT) (touch them when it changes):
# the renderer's, as the lab measures it; the game's logic in main.cpp
# would overflow high work RAM
O2_SRC  ?= saturn/render.cpp openlara/common.cpp $(wildcard renderer/*.cpp)
$(patsubst %,$(BUILD)/obj/%.o,$(O2_SRC)): CXXFLAGS := $(subst $(OPT),-O2,$(CXXFLAGS))

ER      := external_resources
ORIG    := $(ER)/tomb_raider_usa
TRACK1  := $(ORIG)/Tomb Raider (USA) (Track 01).bin
ORIGCUE := $(ORIG)/Tomb Raider (USA).cue
DISC    := $(ER)/disc
MAIN    := $(DISC)/files/0MAIN.BIN
GH_OUT  := build/ghidra

# The game: every platform source plus the OpenLara core and the renderer
GAME_SRC := $(wildcard saturn/*.c saturn/*.cpp saturn/*.s) openlara/common.cpp \
	$(wildcard renderer/*.cpp renderer/*.s)

DISC_TXT := ABS.TXT=disc/ABS.TXT BIB.TXT=disc/BIB.TXT CPY.TXT=disc/CPY.TXT

# $(call DISC_RULE,dir,extra entries): dir/disc/tomb.cue holding dir/0MAIN.BIN
define DISC_RULE
$(1)/disc/tomb.cue: $(1)/0MAIN.BIN $(wildcard disc/*) tools/mkdisc.py $(DISC)/filelist.txt $(foreach e,$(2),$(lastword $(subst =, ,$(e))))
	$$(PY) tools/mkdisc.py --ip $(DISC)/IP.BIN --cue "$(ORIGCUE)" --out $(1)/disc --name tomb \
		0MAIN.BIN=$(1)/0MAIN.BIN $(DISC_TXT) $(2)
endef

# The flags compiled with, in a file rewritten when they change, which every
# object depends on: a build with other testing flags compiles everything.
FLAGS_FILE := $(BUILD)/gen/flags.txt
$(shell mkdir -p $(BUILD)/gen && echo '$(CXXFLAGS)' | cmp -s - $(FLAGS_FILE) || echo '$(CXXFLAGS)' > $(FLAGS_FILE))

# SCU DSP programs, assembled into headers (tools/dspasm.py): saturn/NAME.dsp,
# and those renderer/gen_NAME.py writes, to NAME_dsp.h holding NAME_dsp[]
DSP_HEADERS := $(patsubst saturn/%.dsp,$(BUILD)/gen/%_dsp.h,$(wildcard saturn/*.dsp)) \
	$(patsubst renderer/gen_%.py,$(BUILD)/gen/%_dsp.h,$(wildcard renderer/gen_*.py))
# draw order hints (tools/depth_hints.py): saturn/depth_hints.txt
HEADERS := $(wildcard saturn/*.h openlara/*.h openlara/*/*.h renderer/*.h) $(DSP_HEADERS) $(BUILD)/gen/depth_hints.h \
	$(FLAGS_FILE)

.PHONY: all disc dspbench lab extract ghidra check-toolchain

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

$(BUILD)/obj/%.s.o: %.s $(FLAGS_FILE) | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/gen/%_dsp.h: saturn/%.dsp tools/dspasm.py
	@mkdir -p $(@D)
	$(PY) tools/dspasm.py $< $@ $*_dsp

$(BUILD)/gen/%.dsp: renderer/gen_%.py
	@mkdir -p $(@D)
	$(PY) $< $@

$(BUILD)/gen/%_dsp.h: $(BUILD)/gen/%.dsp tools/dspasm.py
	$(PY) tools/dspasm.py $< $@ $*_dsp

$(BUILD)/gen/depth_hints.h: saturn/depth_hints.txt tools/depth_hints.py
	@mkdir -p $(@D)
	$(PY) tools/depth_hints.py $< $@

%/0MAIN.BIN: %/0MAIN.elf
	$(OBJCOPY) -O binary $< $@

$(BUILD)/0MAIN.elf: $(patsubst %,$(BUILD)/obj/%.o,$(GAME_SRC)) saturn/saturn.ld
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Wl,-Map=$(@D)/0MAIN.map -o $@ $(filter %.o,$^) -lc -lgcc

# Levels packed from the original disc's DATA files (tools/pack_level.py),
# with sound samples from the PlayStation version's levels where they have
# them (external_resources/ps1: the PlayStation disc extracted; optional)
PSXDATA := $(ER)/ps1/files/files/PSXDATA
LEVELS  := TITLE GYM LEVEL1 LEVEL2 LEVEL3A LEVEL3B LEVEL4 LEVEL5 LEVEL6 LEVEL7A LEVEL7B \
           LEVEL8A LEVEL8B LEVEL8C LEVEL10A LEVEL10B LEVEL10C CUT1 CUT2 CUT3 CUT4
GAME_DISC := $(foreach l,$(LEVELS),$(foreach e,PKD TEX SUB SFX ITX PAL MSH,DATA/$(l).$(e)=build/data/$(l).$(e)))
# the title and loading pictures (saturn/screen.h)
PICTURES := USATIT USAOPT GYM224 AZTEC224 GREEK224 EGYPT224 ATLAN224 EIDOS224 CRED224 ENDPG224
GAME_DISC += $(foreach p,$(PICTURES),BINDATA/$(p).BIN=$(DISC)/files/BINDATA/$(p).BIN)
# the original's videos (saturn/fmv.h)
VIDEOS := CORELOGO CAFE MANSION SNOW LIFT VISION CANYON PYRAMID PRISON END
GAME_DISC += $(foreach v,$(VIDEOS),FMV/$(v).CPK=$(DISC)/files/FMV/$(v).CPK)

build/data/%.PKD build/data/%.TEX build/data/%.SUB build/data/%.SFX build/data/%.ITX build/data/%.PAL build/data/%.MSH &: \
		tools/pack_level.py tools/trsat.py tools/trpsx.py tools/sfx.py tools/meshstore.py tools/facepack.py \
		openlara/common.h saturn/vram.h saturn/sound.h $(DISC)/filelist.txt $(wildcard $(PSXDATA)/*.PSX)
	$(PY) tools/pack_level.py $(DISC)/files/DATA $* build/data/$*.PKD $(PSXDATA)

disc: $(BUILD)/disc/tomb.cue
$(eval $(call DISC_RULE,$(BUILD),$(GAME_DISC)))

# The SCU DSP benchmark (dspbench/bench.cpp): a program of its own, on a disc
# of its own: make dspbench
BENCH := build/dspbench
BENCH_SRC := $(wildcard dspbench/*.cpp dspbench/*.s) saturn/font.cpp saturn/dsp.cpp lab/gamemeshxform.s
BENCH_DSP := $(patsubst dspbench/%.dsp,$(BENCH)/gen/%_dsp.h,$(wildcard dspbench/*.dsp))
# and those dspbench/gen_NAME.py writes
BENCH_DSP += $(patsubst dspbench/gen_%.py,$(BENCH)/gen/%_dsp.h,$(wildcard dspbench/gen_*.py))
BENCH_FLAGS := -m2 -mb -O2 -Wall -Idspbench -Isaturn -I$(BENCH)/gen -ffunction-sections
BENCH_CXXFLAGS := $(BENCH_FLAGS) -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-use-cxa-atexit \
	-fno-asynchronous-unwind-tables

$(BENCH)/obj/%.cpp.o: %.cpp $(wildcard dspbench/*.h) saturn/hw.h saturn/dsp.h saturn/font.h $(BENCH_DSP) | check-toolchain
	@mkdir -p $(@D)
	$(CXX) $(BENCH_CXXFLAGS) -c $< -o $@

$(BENCH)/obj/%.s.o: %.s | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(BENCH_FLAGS) -c $< -o $@

$(BENCH)/gen/%_dsp.h: dspbench/%.dsp tools/dspasm.py
	@mkdir -p $(@D)
	$(PY) tools/dspasm.py $< $@ $*_dsp

$(BENCH)/gen/%.dsp: dspbench/gen_%.py dspbench/dspsched.py
	@mkdir -p $(@D)
	$(PY) $< $@

$(BENCH)/gen/meshlight2nodma.dsp: dspbench/meshlight2.dsp

$(BENCH)/gen/%_dsp.h: $(BENCH)/gen/%.dsp tools/dspasm.py
	$(PY) tools/dspasm.py $< $@ $*_dsp

$(BENCH)/0MAIN.elf: $(patsubst %,$(BENCH)/obj/%.o,$(BENCH_SRC)) saturn/saturn.ld
	$(CXX) $(BENCH_CXXFLAGS) $(LDFLAGS) -Wl,-Map=$(@D)/0MAIN.map -o $@ $(filter %.o,$^) -lc -lgcc

dspbench: $(BENCH)/disc/tomb.cue
$(eval $(call DISC_RULE,$(BENCH),))

# The clean-room renderer's lab (lab/lab.cpp): a program of its own, on a disc
# of its own with a captured frame (tools/capture.py) packed as its scene:
# make lab [LAB_SCENE=name of build/captures/name] [LAB=its build: tools/gauntlet.py's, one a scene]
LAB ?= build/lab
LAB_SCENE ?= ledge
LAB_SCENE_FILE := $(LAB)/$(shell echo $(LAB_SCENE) | tr a-z A-Z).SCN
LAB_SRC := $(wildcard renderer/*.cpp renderer/*.s lab/*.cpp lab/*.s) saturn/font.cpp saturn/cd.cpp saturn/dsp.cpp
# DSP programs renderer/gen_NAME.py writes, assembled into NAME_dsp.h; and the
# bench's v3 mesh program
LAB_DSP := $(patsubst renderer/gen_%.py,$(LAB)/gen/%_dsp.h,$(wildcard renderer/gen_*.py)) $(LAB)/gen/meshlight3_dsp.h
# -g: lines for profiles (tools/lab.py's trace), and the assembly's local
# labels kept, for the places inside its macros; the code is the same without
LAB_FLAGS := -m2 -mb -O2 -g -Wall -DRENDER_LAB -Ilab -Irenderer -Isaturn -I$(LAB)/gen -ffunction-sections
LAB_CXXFLAGS := $(LAB_FLAGS) -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-use-cxa-atexit \
	-fno-asynchronous-unwind-tables

$(LAB)/obj/%.cpp.o: %.cpp $(wildcard lab/*.h lab/*.inc renderer/*.h) saturn/hw.h saturn/font.h saturn/cd.h saturn/dsp.h $(LAB_DSP) \
		| check-toolchain
	@mkdir -p $(@D)
	$(CXX) $(LAB_CXXFLAGS) -c $< -o $@

# the game's pose, light, rooms seen and draw list, compiled as the game compiles
# draw.h and room.h (main.cpp, $(OPT))
$(LAB)/obj/lab/gamepose.cpp.o $(LAB)/obj/lab/gamelight.cpp.o $(LAB)/obj/lab/gamerooms.cpp.o $(LAB)/obj/lab/gamedraws.cpp.o: \
	LAB_CXXFLAGS := $(subst -O2,$(OPT),$(LAB_CXXFLAGS))

$(LAB)/gen/%.dsp: renderer/gen_%.py
	@mkdir -p $(@D)
	$(PY) $< $@

$(LAB)/gen/meshlight3.dsp: dspbench/gen_meshlight3.py
	@mkdir -p $(@D)
	$(PY) $< $@

$(LAB)/gen/%_dsp.h: $(LAB)/gen/%.dsp tools/dspasm.py
	$(PY) tools/dspasm.py $< $@ $*_dsp

$(LAB)/obj/%.s.o: %.s | check-toolchain
	@mkdir -p $(@D)
	$(CC) $(LAB_FLAGS) -Wa,--gdwarf-2 -Wa,-L -c $< -o $@

$(LAB)/0MAIN.elf: $(patsubst %,$(LAB)/obj/%.o,$(LAB_SRC)) saturn/saturn.ld
	$(CXX) $(LAB_CXXFLAGS) $(LDFLAGS) -Wl,-Map=$(@D)/0MAIN.map -o $@ $(filter %.o,$^) -lc -lgcc

$(LAB_SCENE_FILE): tools/labscene.py tools/capture.py tools/facepack.py tools/meshstore.py tools/posepack.py tools/roompack.py 		tools/drawpack.py \
		tools/gamelayout.py \
		openlara/common.h $(wildcard build/captures/$(LAB_SCENE)/*)
	@mkdir -p $(@D)
	$(PY) tools/labscene.py $(LAB_SCENE) $@

lab: $(LAB)/disc/tomb.cue
$(eval $(call DISC_RULE,$(LAB),SCENE.SCN=$(LAB_SCENE_FILE)))

# the scene chosen, in a file rewritten when it changes: the disc is remade
# for another scene older than it
LAB_SCENE_CHOICE := $(LAB)/scene.txt
$(shell mkdir -p $(LAB) && echo '$(LAB_SCENE)' | cmp -s - $(LAB_SCENE_CHOICE) || echo '$(LAB_SCENE)' > $(LAB_SCENE_CHOICE))
$(LAB)/disc/tomb.cue: $(LAB_SCENE_CHOICE)

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
