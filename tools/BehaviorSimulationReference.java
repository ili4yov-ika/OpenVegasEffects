// Read-only extraction of Project.dll's Behavior simulation and transform cache.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import java.nio.file.*;

public class BehaviorSimulationReference extends GhidraScript {
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        for (String address : new String[]{
                "1803738c0", "180376470", "18036d5f0", "18036d6c0"}) {
            Function function = getFunctionAt(toAddr(address));
            out.append("\n// ").append(address).append(" ")
               .append(function == null ? "<not a function>" : function.getName()).append("\n");
            if (function != null) {
                var result = decompiler.decompileFunction(function, 300, monitor);
                out.append(result.decompileCompleted()
                    ? result.getDecompiledFunction().getC()
                    : result.getErrorMessage());
            }
        }
        out.append("\n// Simulation callback symbols and vtables\n");
        SymbolIterator symbols = currentProgram.getSymbolTable().getAllSymbols(true);
        while (symbols.hasNext()) {
            Symbol symbol = symbols.next();
            String name = symbol.getName(true);
            if (!name.contains("51740a8710c70bcdd37c37c5da251e73")
                && !name.contains("34ea78db5d7b42385139c93742961c50")) {
                continue;
            }
            Address address = symbol.getAddress();
            out.append("\n// SYMBOL ").append(address).append(" ").append(name).append("\n");
            if (name.contains("vftable")) {
                for (int slot = 0; slot < 6; ++slot) {
                    long pointer = getLong(address.add(slot * 8L));
                    Address target = toAddr(pointer);
                    Function function = getFunctionAt(target);
                    if (function == null) function = getFunctionContaining(target);
                    out.append("// slot ").append(slot).append(" -> ").append(target)
                       .append(" ").append(function == null ? "<no function>" : function.getName())
                       .append("\n");
                    if (function != null) {
                        var result = decompiler.decompileFunction(function, 300, monitor);
                        out.append(result.decompileCompleted()
                            ? result.getDecompiledFunction().getC()
                            : result.getErrorMessage());
                    }
                }
            }
        }
        decompiler.dispose();
        Files.writeString(Path.of(getScriptArgs()[0]), out.toString());
        println("Behavior simulation reference written");
    }
}
