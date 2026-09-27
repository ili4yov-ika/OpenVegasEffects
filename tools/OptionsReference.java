// Read-only extraction of the VEGAS Effects Preferences page builders used by
// MARKDOWN/RE_wnd_Options. The output is intentionally kept in build/.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import java.nio.file.*;

public class OptionsReference extends GhidraScript {
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        String[] functions = {
            "1403a24f0", // General
            "1403a6bb0", // Display / editor / viewer
            "1403ac370", // Render / video / 3D
            "1403b13c0", // Prompts & warnings
            "1403b7ed0", // Cache
            "1403bec70", // Proxies & pre-renders
            "1403c26f0", // Auto save
            "1403c6560", // Export
            "1403c8a40"  // Interface
        };
        for (String address : functions) {
            Function function = getFunctionAt(toAddr(address));
            out.append("\n// ").append(address).append(' ')
               .append(function == null ? "<not a function>" : function.getName()).append('\n');
            if (function != null) {
                var result = decompiler.decompileFunction(function, 240, monitor);
                out.append(result.decompileCompleted()
                    ? result.getDecompiledFunction().getC() : result.getErrorMessage());
            }
        }
        decompiler.dispose();
        Files.writeString(Path.of(getScriptArgs()[0]), out.toString());
    }
}
