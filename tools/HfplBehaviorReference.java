// Read-only extraction of a Behavior module's exported dispatcher. Run this
// after importing one .hfpl into a temporary Ghidra project.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;
import java.nio.file.*;

public class HfplBehaviorReference extends GhidraScript {
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        SymbolIterator symbols = currentProgram.getSymbolTable().getSymbols("Notify");
        while (symbols.hasNext()) {
            Symbol symbol = symbols.next();
            Function function = getFunctionAt(symbol.getAddress());
            if (function == null) continue;
            out.append("// ").append(symbol.getAddress()).append(" ")
               .append(symbol.getName(true)).append("\n");
            var result = decompiler.decompileFunction(function, 300, monitor);
            out.append(result.decompileCompleted()
                ? result.getDecompiledFunction().getC()
                : result.getErrorMessage());
        }
        for (String address : getScriptArgs().length > 1
                ? getScriptArgs()[1].split(",") : new String[0]) {
            Function function = getFunctionAt(toAddr(address.trim()));
            if (function == null) continue;
            out.append("\n// ").append(address).append(" ")
               .append(function.getName()).append("\n");
            var result = decompiler.decompileFunction(function, 300, monitor);
            out.append(result.decompileCompleted()
                ? result.getDecompiledFunction().getC()
                : result.getErrorMessage());
        }
        decompiler.dispose();
        Files.writeString(Path.of(getScriptArgs()[0]), out.toString());
        println("HFPL Behavior reference written");
    }
}
