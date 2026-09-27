// Read-only extraction of the shared parameter-editor builders used by Controls.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import java.nio.file.*;
public class ControlsReference extends GhidraScript {
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        for (String address : new String[]{"140478f90", "14072b930", "140746580", "1404b91f0", "1404bc6e0"}) {
            Function function = getFunctionAt(toAddr(address));
            out.append("\n// ").append(address).append(" ")
               .append(function == null ? "<not a function>" : function.getName()).append("\n");
            if (function != null) {
                var result = decompiler.decompileFunction(function, 120, monitor);
                out.append(result.decompileCompleted() ? result.getDecompiledFunction().getC() : result.getErrorMessage());
            }
        }
        out.append("\n// referenced strings\n");
        for (String address : new String[]{"1412f5490", "1412f54a8", "1412f54b8",
                "1412fa800", "1412fa818", "1412fa830", "1412fa848", "1412fa858", "1412fa868"}) {
            var data = getDataAt(toAddr(address));
            out.append(address).append(": ").append(data == null ? "<undefined>" : data.getValue()).append("\n");
        }
        decompiler.dispose();
        Files.writeString(Path.of(getScriptArgs()[0]), out.toString());
        println("Controls reference written");
    }
}
