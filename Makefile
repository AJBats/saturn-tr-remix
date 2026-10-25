# Tomb Raider Saturn remaster
#
# external_resources/ (gitignored) must contain the Tomb Raider (USA) Redump
# image extracted to external_resources/tomb_raider_usa/.
#
#   make disc     build/disc/tomb.cue (our 0MAIN.BIN + the original audio tracks)
#   make ghidra   headless analysis of the original 0MAIN.BIN

PY      ?= python -I
GHIDRA  ?= C:/Users/albat/Documents/ghidra_12.0_PUBLIC_20251205/ghidra_12.0_PUBLIC
SH_BIN  ?= C:/Users/albat/saturndev/saturn-sdk/toolchain/bin

# cc1.exe loads its DLLs from the toolchain's bin directory via PATH.
export PATH := $(SH_BIN);$(PATH)

CC      := sh-elf-gcc
CXX     := sh-elf-g++
OBJCOPY := sh-elf-objcopy
CFLAGS  := -m2 -mb -O2 -Wall -D__SATURN__ -Isaturn -Iopenlara
CXXFLAGS := $(CFLAGS) -fno-rtti -fno-exceptions -fno-threadsafe-statics -fno-use-cxa-atexit 	-fno-asynchronous-unwind-tables -Wno-class-memaccess
LDFLAGS := -nostdlib -T saturn/saturn.ld -Wl,-Map=build/0MAIN.map

ER      := external_resources
ORIG    := $(ER)/tomb_raider_usa
TRACK1  := $(ORIG)/Tomb Raider (USA) (Track 01).bin
ORIGCUE := $(ORIG)/Tomb Raider (USA).cue
DISC    := $(ER)/disc
MAIN    := $(DISC)/files/0MAIN.BIN
GH_OUT  := build/ghidra

SAT_SRC := $(wildcard saturn/*.c saturn/*.cpp saturn/*.s) openlara/common.cpp
SAT_OBJ := $(patsubst %,build/obj/%.o,$(SAT_SRC))
HEADERS := $(wildcard saturn/*.h openlara/*.h openlara/*/*.h)

.PHONY: all disc extract ghidra

all: disc

build/obj/%.c.o: %.c $(HEADERS)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

build/obj/%.cpp.o: %.cpp $(HEADERS)
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

build/obj/%.s.o: %.s
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

build/0MAIN.elf: $(SAT_OBJ) saturn/saturn.ld
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o $@ $(SAT_OBJ) -lc -lgcc

build/0MAIN.BIN: build/0MAIN.elf
	$(OBJCOPY) -O binary $< $@

disc: build/disc/tomb.cue

build/disc/tomb.cue: build/0MAIN.BIN $(wildcard disc/*) tools/mkdisc.py $(DISC)/filelist.txt
	$(PY) tools/mkdisc.py --ip $(DISC)/IP.BIN --cue "$(ORIGCUE)" --out build/disc --name tomb \
		0MAIN.BIN=build/0MAIN.BIN \
		ABS.TXT=disc/ABS.TXT BIB.TXT=disc/BIB.TXT CPY.TXT=disc/CPY.TXT

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
