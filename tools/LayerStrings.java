import ghidra.app.script.GhidraScript;
import java.nio.file.*;
public class LayerStrings extends GhidraScript {
    public void run() throws Exception {
        StringBuilder out = new StringBuilder();
        for (long address = 0x141322200L; address < 0x1413223b0L; address += 8) {
            var data = getDataAt(toAddr(address));
            if (data != null) out.append(Long.toHexString(address)).append(": ")
                .append(data.getValue()).append("\n");
        }
        Files.writeString(Path.of(getScriptArgs()[0]), out.toString());
    }
}
