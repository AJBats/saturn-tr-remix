// Pre-analysis script: lays out the Saturn memory map around a raw program
// image loaded at 0x06004000 and marks its entry point.
//@category Saturn

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.SourceType;

public class SaturnSetup extends GhidraScript {

    private static final long LOAD_ADDR = 0x06004000L;
    // End of the last function's literal pool in 0MAIN.BIN; .data follows.
    private static final long DATA_START = 0x06060bc8L;
    private static final long CACHE_THROUGH = 0x20000000L;

    private MemoryBlock uninit(String name, long start, long size, boolean isVolatile) throws Exception {
        MemoryBlock b = currentProgram.getMemory().createUninitializedBlock(name, toAddr(start), size, false);
        b.setPermissions(true, true, false);
        b.setVolatile(isVolatile);
        return b;
    }

    // Cache-through alias (+0x20000000) of an existing block.
    private void mirror(MemoryBlock b) throws Exception {
        Memory mem = currentProgram.getMemory();
        long start = b.getStart().getOffset();
        MemoryBlock m = mem.createByteMappedBlock(b.getName() + "_CT", toAddr(start + CACHE_THROUGH),
                b.getStart(), b.getSize(), false);
        m.setPermissions(true, true, b.isExecute());
    }

    @Override
    public void run() throws Exception {
        Memory mem = currentProgram.getMemory();
        MemoryBlock main = mem.getBlock(toAddr(LOAD_ADDR));
        long end = main.getEnd().getOffset() + 1;
        // Read-only text lets the decompiler fold literal-pool loads into
        // direct calls and constants.
        mem.split(main, toAddr(DATA_START));
        MemoryBlock text = mem.getBlock(toAddr(LOAD_ADDR));
        MemoryBlock data = mem.getBlock(toAddr(DATA_START));
        text.setName("TEXT");
        text.setPermissions(true, false, true);
        data.setName("DATA");
        data.setPermissions(true, true, false);

        MemoryBlock hwramLo = uninit("HWRAM_SYS", 0x06000000L, LOAD_ADDR - 0x06000000L, false);
        MemoryBlock hwramHi = uninit("BSS", end, 0x06100000L - end, false);
        MemoryBlock lwram = uninit("LWRAM", 0x00200000L, 0x100000L, false);
        mirror(hwramLo);
        mirror(text);
        mirror(data);
        mirror(hwramHi);
        mirror(lwram);

        uninit("SMPC", 0x20100000L, 0x80L, true);
        uninit("BACKUP_RAM", 0x20180000L, 0x10000L, true);
        uninit("MINIT", 0x21000000L, 0x2L, true);
        uninit("SINIT", 0x21800000L, 0x2L, true);
        uninit("SOUND_RAM", 0x25A00000L, 0x80000L, true);
        uninit("SCSP_REGS", 0x25B00000L, 0x1000L, true);
        uninit("VDP1_VRAM", 0x25C00000L, 0x80000L, true);
        uninit("VDP1_FB", 0x25C80000L, 0x40000L, true);
        uninit("VDP1_REGS", 0x25D00000L, 0x20L, true);
        uninit("VDP2_VRAM", 0x25E00000L, 0x80000L, true);
        uninit("VDP2_CRAM", 0x25F00000L, 0x1000L, true);
        uninit("VDP2_REGS", 0x25F80000L, 0x120L, true);
        uninit("SCU_REGS", 0x25FE0000L, 0x100L, true);

        Address entry = toAddr(LOAD_ADDR);
        currentProgram.getSymbolTable().addExternalEntryPoint(entry);
        disassemble(entry);
        createFunction(entry, "_start");
        currentProgram.getSymbolTable().createLabel(entry, "_start", SourceType.USER_DEFINED);
    }
}
