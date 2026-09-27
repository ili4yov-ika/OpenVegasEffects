// Read-only extraction of LayerPanel/CompositionPanel builders and layer commands.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import java.nio.file.*;

public class LayerReference extends GhidraScript {
    public void run() throws Exception {
        DecompInterface decompiler = new DecompInterface();
        decompiler.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        for (String address : new String[]{
                "140219ad0", "1408cf1b0", "1408ceb40", "1408ce4b0", "1408d09d0",
                "14054acf0", "140507ce0", "1405040c0"}) {
            Function function = getFunctionAt(toAddr(address));
            out.append("\n// ").append(address).append(" ")
               .append(function == null ? "<not a function>" : function.getName()).append("\n");
            if (function != null) {
                var result = decompiler.decompileFunction(function, 180, monitor);
                out.append(result.decompileCompleted()
                    ? result.getDecompiledFunction().getC() : result.getErrorMessage());
            }
        }
        out.append("\n// referenced strings\n");
        for (String address : new String[]{
                "1412c4da0", "1412c2bec", "141322378", "1412d92e0", "1412d9250",
                "1412ff770", "1412ff7f8", "1412ff8b0", "1412ff8c0", "1412d91d8",
                "1412eee50", "141301708", "1413000b0", "1412e1338", "141303b58"}) {
            var data = getDataAt(toAddr(address));
            out.append(address).append(": ")
               .append(data == null ? "<undefined>" : data.getValue()).append("\n");
        }
        decompiler.dispose();
        Files.writeString(Path.of(getScriptArgs()[0]), out.toString());
        println("Layer reference written");
    }
}
