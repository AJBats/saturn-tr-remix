// Post-analysis script: creates functions that call-following missed, i.e.
// those reached only through pointer tables or register jumps. Two sources:
//   1. orphan "sts.l pr,@-r15" prologues in TEXT, extended back over pushes
//   2. 32-bit words in TEXT or DATA that point at an orphan push prologue
// Repeats with re-analysis until no new functions appear.
//@category Saturn

import java.util.TreeSet;

import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.Memory;
import ghidra.program.model.mem.MemoryBlock;

public class FindFunctions extends GhidraScript {

    private Memory mem;
    private MemoryBlock text;
    private MemoryBlock data;

    private int word(long addr) throws Exception {
        return mem.getShort(toAddr(addr)) & 0xffff;
    }

    // mov.l Rn,@-r15 / sts.l mach,@-r15 / sts.l macl,@-r15 / stc.l gbr,@-r15
    private static boolean isPush(int w) {
        return (w & 0xff0f) == 0x2f06 || w == 0x4f02 || w == 0x4f12 || w == 0x4f13;
    }

    private static boolean isPrologue(int w) {
        return w == 0x4f22 || isPush(w);
    }

    // Not inside a function or defined data (it may already be disassembled).
    private boolean orphan(long addr) {
        Address a = toAddr(addr);
        Listing listing = currentProgram.getListing();
        return currentProgram.getFunctionManager().getFunctionContaining(a) == null
                && listing.getDefinedDataContaining(a) == null;
    }

    private TreeSet<Long> candidates() throws Exception {
        long start = text.getStart().getOffset();
        long end = text.getEnd().getOffset();
        TreeSet<Long> found = new TreeSet<>();

        for (long a = start; a + 1 <= end; a += 2) {
            if (word(a) != 0x4f22 || !orphan(a)) {
                continue;
            }
            long s = a;
            while (s - 2 >= start && isPush(word(s - 2)) && orphan(s - 2)) {
                s -= 2;
            }
            found.add(s);
        }

        for (long a = start; a + 3 <= data.getEnd().getOffset(); a += 4) {
            long p = mem.getInt(toAddr(a)) & 0xffffffffL;
            if (p < start || p + 1 > end || (p & 1) != 0) {
                continue;
            }
            if (isPrologue(word(p)) && orphan(p) && !(p - 2 >= start && isPush(word(p - 2)))) {
                found.add(p);
            }
        }
        return found;
    }

    @Override
    public void run() throws Exception {
        mem = currentProgram.getMemory();
        text = mem.getBlock("TEXT");
        data = mem.getBlock("DATA");
        FunctionManager fm = currentProgram.getFunctionManager();
        int before = fm.getFunctionCount();

        for (int pass = 1; pass <= 10; pass++) {
            TreeSet<Long> found = candidates();
            int created = 0;
            for (long a : found) {
                monitor.checkCancelled();
                Address addr = toAddr(a);
                if (!orphan(a)) {
                    continue; // swallowed by a function created earlier in this pass
                }
                boolean code = currentProgram.getListing().getInstructionAt(addr) != null || disassemble(addr);
                if (code && createFunction(addr, null) != null) {
                    created++;
                }
            }
            analyzeChanges(currentProgram);
            println(String.format("FIND pass %d: %d candidates, %d functions created", pass, found.size(), created));
            if (created == 0) {
                break;
            }
        }
        println(String.format("FIND: %d -> %d functions", before, fm.getFunctionCount()));
    }
}
