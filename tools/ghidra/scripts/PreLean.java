// PreLean.java — headless preScript (Java; analyzeHeadless compiles .java natively).
//
// Runs BEFORE auto-analysis to disable the passes we don't need for RTTI + targeted
// decompilation, which otherwise dominate runtime on a 37 MB PE:
//   - Aggressive Instruction Finder: speculatively disassembles gaps, spawning thousands
//     of bogus thunks that feed an O(n^2) function-body fixup (the observed bottleneck).
//   - Decompiler Parameter ID / Switch Analysis: whole-program decompiler passes; our
//     query scripts run their own DecompInterface on the handful of functions we care
//     about, so global signature recovery is wasted work here.
// RTTI + basic disassembly stay ON — that's what gives query scripts the vtable symbols.
//
// NOTE: this is Java, not Python, on purpose — the stock analyzeHeadless launcher has no
// Python interpreter (see docs/ghidra.md). PyGhidra is only for the standalone query scripts.
import ghidra.app.script.GhidraScript;

public class PreLean extends GhidraScript {
    @Override
    public void run() throws Exception {
        String[] off = {
            "Aggressive Instruction Finder",
            "Decompiler Parameter ID",
            "Decompiler Switch Analysis",
        };
        for (String name : off) {
            setAnalysisOption(currentProgram, name, "false");
        }
        println("[PreLean] disabled: " + String.join(", ", off));
    }
}
