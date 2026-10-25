# Tomb Raider Saturn remaster
#
# external_resources/ (gitignored) must contain the Tomb Raider (USA) Redump
# image extracted to external_resources/tomb_raider_usa/.
#
#   make disc     $(BUILD)/disc/tomb.cue (our 0MAIN.BIN + the original audio tracks)
#   make bench    $(BUILD)/bench/disc/tomb.cue (CPU benchmark, see saturn/bench/)
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
CFLAGS  := -m2 -mb $(OPT) -Wall -D__SATURN__ -Isaturn -Iopenlara
# -fno-lifetime-dse: OpenLara sets ItemObj fields and then placement-news the
# subclass over them (ItemObj::init); the constructors read those fields.
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-use-cxa-atexit \
	-fno-asynchronous-unwind-tables -fno-lifetime-dse -Wno-class-memaccess
LDFLAGS := -nostdlib -T saturn/saturn.ld -Wl,--no-warn-rwx-segments

ER      := external_resources
ORIG    := $(ER)/tomb_raider_usa
TRACK1  := $(ORIG)/Tomb Raider (USA) (Track 01).bin
ORIGCUE := $(ORIG)/Tomb Raider (USA).cue
DISC    := $(ER)/disc
MAIN    := $(DISC)/files/0MAIN.BIN
GH_OUT  := build/ghidra

PLATFORM := $(wildcard saturn/*.c saturn/*.cpp saturn/*.s) openlara/common.cpp
GAME_OBJ := $(patsubst %,$(BUILD)/obj/%.o,$(PLATFORM))
BENCH_OBJ := $(patsubst %,$(BUILD)/obj/%.o,$(filter-out saturn/main.cpp,$(PLATFORM)) saturn/bench/bench.cpp)
HEADERS := $(wildcard saturn/*.h openlara/*.h openlara/*/*.h)

.PHONY: all disc bench extract ghidra check-toolchain

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

%/0MAIN.BIN: %/0MAIN.elf
	$(OBJCOPY) -O binary $< $@

%/disc/tomb.cue: %/0MAIN.BIN $(wildcard disc/*) tools/mkdisc.py $(DISC)/filelist.txt
	$(PY) tools/mkdisc.py --ip $(DISC)/IP.BIN --cue "$(ORIGCUE)" --out $*/disc --name tomb \
		0MAIN.BIN=$*/0MAIN.BIN \
		ABS.TXT=disc/ABS.TXT BIB.TXT=disc/BIB.TXT CPY.TXT=disc/CPY.TXT

# The game.
$(BUILD)/0MAIN.elf: $(GAME_OBJ) saturn/saturn.ld
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Wl,-Map=$(BUILD)/0MAIN.map -o $@ $(GAME_OBJ) -lc -lgcc

disc: $(BUILD)/disc/tomb.cue

# CPU benchmark of engine hot paths, for comparing compilers and flags.
$(BUILD)/bench/0MAIN.elf: $(BENCH_OBJ) saturn/saturn.ld
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -Wl,-Map=$(BUILD)/bench/0MAIN.map -o $@ $(BENCH_OBJ) -lc -lgcc

bench: $(BUILD)/bench/disc/tomb.cue

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
