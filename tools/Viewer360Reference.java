// Read-only extraction of the 360 Viewer host, UI, controller and view list.
import ghidra.app.script.GhidraScript;
import ghidra.app.decompiler.DecompInterface;
import ghidra.program.model.listing.Function;
import java.nio.file.*;
public class Viewer360Reference extends GhidraScript {
    public void run() throws Exception {
        DecompInterface d = new DecompInterface(); d.openProgram(currentProgram);
        StringBuilder out = new StringBuilder();
        for (String address : new String[]{"1401cfd70", "1401cf680", "1402910f0",
                "1402906d0", "140290e50", "140380680", "140380b60", "140241a20"}) {
            Function f = getFunctionAt(toAddr(address));
            out.append("\n// ").append(address).append(" ")
               .append(f == null ? "<not a function>" : f.getName()).append("\n");
            if (f != null) {
                var r = d.decompileFunction(f, 180, monitor);
                out.append(r.decompileCompleted() ? r.getDecompiledFunction().getC() : r.getErrorMessage());
            }
        }
        out.append("\n// strings\n");
        for (String address : new String[]{"1412bc7d0", "1412bc810", "1412cb210",
                "1412d1150", "1412d115c", "1412d1168", "1412d1180", "1412d11a0",
                "1412d11c0", "1412d11d0", "1412d11f0", "1412d1208", "1412d121c",
                "1412d1228", "1412d1230", "141503ee8"}) {
            var data = getDataAt(toAddr(address));
            out.append(address).append(": ").append(data == null ? "<undefined>" : data.getValue()).append("\n");
        }
        d.dispose(); Files.writeString(Path.of(getScriptArgs()[0]), out.toString());
    }
}
