#!/usr/bin/env python3
# decompile_at.py — decompile arbitrary addresses in the analysed (or scratch) project.
#
# General-purpose follow-up query: given one or more VAs (hex), disassemble + create a
# function at each (no full analysis needed) and dump decompilation + a call-site index.
# Use to chase the helpers a parent function calls (e.g. "is this call a world raycast?").
#
# Run: ghidra.sh query decompile_at.py 0x1407ecbf0 0x140d19e40 ...
#   (opens GHIDRA_PROJ_NAME read/write in the project loc; defaults to SkyrimScratch so it
#    pairs with find_via_rtti.py's unpacked import.)

import os
import sys

import pyghidra

REPO = "/home/mse/Projects/skyrim-headless-mods"
PROJ_LOC = os.environ.get("GHIDRA_PROJ_LOC", os.path.join(REPO, "tools/ghidra/projects"))
PROJ_NAME = os.environ.get("GHIDRA_PROJ_NAME", "SkyrimScratch")
OUT = os.environ.get("GHIDRA_OUT", os.path.join(REPO, "tools/ghidra/out"))

addrs = [int(a, 16) for a in sys.argv[1:]]
if not addrs:
    print("usage: decompile_at.py <hexaddr> [hexaddr ...]", file=sys.stderr)
    sys.exit(2)

os.makedirs(OUT, exist_ok=True)
pyghidra.start()

from ghidra.base.project import GhidraProject            # noqa: E402
from ghidra.app.decompiler import DecompInterface        # noqa: E402
from ghidra.app.cmd.disassemble import DisassembleCommand # noqa: E402
from ghidra.app.cmd.function import CreateFunctionCmd     # noqa: E402
from ghidra.util.task import ConsoleTaskMonitor           # noqa: E402

gp = GhidraProject.openProject(PROJ_LOC, PROJ_NAME, True)
root = gp.getProject().getProjectData().getRootFolder()
prog = gp.openProgram("/", list(root.getFiles())[0].getName(), False)  # read/write (we disassemble)

space = prog.getAddressFactory().getDefaultAddressSpace()
listing = prog.getListing()
fm = prog.getFunctionManager()
mon = ConsoleTaskMonitor()
ifc = DecompInterface()
ifc.openProgram(prog)


def A(v):
    return space.getAddress(v & 0xFFFFFFFFFFFFFFFF)


for va in addrs:
    ui = A(va)
    print("\n==================== 0x%x ====================" % va)
    if listing.getInstructionAt(ui) is None:
        txid = prog.startTransaction("disasm")
        try:
            DisassembleCommand(ui, None, True).applyTo(prog, mon)
            CreateFunctionCmd(ui).applyTo(prog, mon)
        finally:
            prog.endTransaction(txid, True)
    func = fm.getFunctionAt(ui) or fm.getFunctionContaining(ui)
    if func is None:
        print("  !! no function formed")
        continue
    res = ifc.decompileFunction(func, 180, mon)
    cpath = os.path.join(OUT, "fn_%x.c" % va)
    with open(cpath, "w") as fh:
        fh.write(res.getDecompiledFunction().getC() if (res and res.decompileCompleted())
                 else "// decompile failed\n")
    ninsns = sum(1 for _ in listing.getInstructions(func.getBody(), True))
    calls = []
    for ins in listing.getInstructions(func.getBody(), True):
        if ins.getMnemonicString().upper().startswith("CALL"):
            ta = next((r.getToAddress() for r in ins.getReferencesFrom()), None)
            calls.append("%s -> %s" % (ins.getAddress(), ta if ta else "(indirect)"))
    print("  %s  insns=%d calls=%d -> out/fn_%x.c" % (func.getName(), ninsns, len(calls), va))
    for c in calls:
        print("    %s" % c)

gp.close()
