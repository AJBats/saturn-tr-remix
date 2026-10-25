# Tomb Raider Saturn remaster
#
# external_resources/ (gitignored) must contain the Tomb Raider (USA) Redump
# image extracted to external_resources/tomb_raider_usa/.

PY      ?= python -I
GHIDRA  ?= C:/Users/albat/Documents/ghidra_12.0_PUBLIC_20251205/ghidra_12.0_PUBLIC

ER      := external_resources
TRACK1  := $(ER)/tomb_raider_usa/Tomb Raider (USA) (Track 01).bin
DISC    := $(ER)/disc
MAIN    := $(DISC)/files/0MAIN.BIN
GH_OUT  := build/ghidra

.PHONY: extract ghidra

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
