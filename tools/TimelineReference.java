// Read-only extraction of the timeline toolbar and its resource references.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import java.nio.file.*;
public class TimelineReference extends GhidraScript {
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        for (String address : new String[]{"1402eaf60", "140507ce0", "1405040c0"}) {
            Function function = getFunctionAt(toAddr(address));
            out.append("\n// ").append(address).append("\n");
            if (function != null) {
                var result = decompiler.decompileFunction(function, 120, monitor);
                out.append(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
            }
        }
        decompiler.dispose();
        Files.writeString(Path.of(getScriptArgs()[0]), out.toString());
        println("Timeline reference written");
    }
}
