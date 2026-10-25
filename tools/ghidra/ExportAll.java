// Post-analysis script: exports the function list, decompiled C and a
// disassembly listing of the TEXT and DATA blocks.
// Script argument: output directory.
//@category Saturn

import java.io.File;
import java.io.FileWriter;
import java.io.PrintWriter;

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileOptions;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.address.AddressSet;
import ghidra.program.model.listing.CodeUnit;
import ghidra.program.model.listing.CodeUnitIterator;
import ghidra.program.model.listing.Data;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;
import ghidra.program.model.listing.Listing;
import ghidra.program.model.mem.MemoryBlock;
import ghidra.program.model.symbol.Symbol;

public class ExportAll extends GhidraScript {

    private static String hex(byte[] bytes) {
        StringBuilder sb = new StringBuilder();
        for (byte b : bytes) {
            sb.append(String.format("%02x", b & 0xff));
        }
        return sb.toString();
    }

    @Override
    public void run() throws Exception {
        File out = new File(getScriptArgs()[0]);
        out.mkdirs();
        Listing listing = currentProgram.getListing();
        FunctionManager fm = currentProgram.getFunctionManager();
        MemoryBlock text = currentProgram.getMemory().getBlock("TEXT");
        MemoryBlock data = currentProgram.getMemory().getBlock("DATA");

        long funcBytes = 0;
        int funcCount = 0;
        try (PrintWriter w = new PrintWriter(new FileWriter(new File(out, "functions.tsv")))) {
            w.println("address\tsize\tname\tcallers\tcallees");
            for (Function f : fm.getFunctions(true)) {
                long size = f.getBody().getNumAddresses();
                funcBytes += size;
                funcCount++;
                w.printf("%08x\t%d\t%s\t%d\t%d%n", f.getEntryPoint().getOffset(), size, f.getName(),
                        f.getCallingFunctions(monitor).size(), f.getCalledFunctions(monitor).size());
            }
        }

        // Program options include "respect read-only flags", which folds
        // literal-pool loads from TEXT into direct calls and constants.
        DecompileOptions options = new DecompileOptions();
        options.grabFromProgram(currentProgram);
        DecompInterface ifc = new DecompInterface();
        ifc.setOptions(options);
        ifc.openProgram(currentProgram);
        int failed = 0;
        try (PrintWriter w = new PrintWriter(new FileWriter(new File(out, "decomp.c")))) {
            for (Function f : fm.getFunctions(true)) {
                monitor.checkCancelled();
                DecompileResults r = ifc.decompileFunction(f, 120, monitor);
                w.printf("// ===== %s @ %08x =====%n", f.getName(), f.getEntryPoint().getOffset());
                if (r.decompileCompleted()) {
                    w.println(r.getDecompiledFunction().getC());
                } else {
                    failed++;
                    w.println("// decompile failed: " + r.getErrorMessage());
                    w.println();
                }
            }
        }
        ifc.dispose();

        try (PrintWriter w = new PrintWriter(new FileWriter(new File(out, "main.s")))) {
            CodeUnitIterator it = listing.getCodeUnits(new AddressSet(text.getStart(), data.getEnd()), true);
            StringBuilder raw = new StringBuilder();
            Address rawStart = null;
            while (it.hasNext()) {
                CodeUnit cu = it.next();
                Address a = cu.getAddress();
                Symbol s = currentProgram.getSymbolTable().getPrimarySymbol(a);
                boolean undefined = cu instanceof Data && !((Data) cu).isDefined();
                // Runs of undefined bytes are packed 16 per line until a label interrupts them.
                if (rawStart != null && (!undefined || s != null || raw.length() >= 32)) {
                    w.printf("  %08x  .byte %s%n", rawStart.getOffset(), raw);
                    raw.setLength(0);
                    rawStart = null;
                }
                if (s != null) {
                    w.printf("%n%s:%n", s.getName());
                }
                if (undefined) {
                    if (rawStart == null) {
                        rawStart = a;
                    }
                    raw.append(hex(cu.getBytes()));
                } else {
                    w.printf("  %08x  %-12s  %s%n", a.getOffset(), hex(cu.getBytes()), cu.toString());
                }
            }
            if (rawStart != null) {
                w.printf("  %08x  .byte %s%n", rawStart.getOffset(), raw);
            }
        }

        println(String.format("EXPORT: %d functions, %d bytes in functions (%.1f%% of %d-byte TEXT), %d decompile failures",
                funcCount, funcBytes, 100.0 * funcBytes / text.getSize(), text.getSize(), failed));
    }
}
