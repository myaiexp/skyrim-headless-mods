#!/usr/bin/env python3
# disasm_at.py — print the instructions at an address, function or not.
#
# The fallback when decompile_at.py says "no function formed": CreateFunctionCmd refuses
# some perfectly real call targets (a leaf that another function's flow already swallowed,
# a thunk, a mid-block entry), and the decompiler needs a Function. This needs only bytes:
# disassemble at the address if nothing is there yet, then walk forward printing up to
# --n instructions, stopping after the first RET (or an unconditional JMP when -j is set).
#
# Run: ghidra.sh query disasm_at.py 0x140cd5410 [0x... ] [--n 60] [-j]

import os
import sys

import pyghidra

REPO = "/home/mse/Projects/skyrim-headless-mods"
PROJ_LOC = os.environ.get("GHIDRA_PROJ_LOC", os.path.join(REPO, "tools/ghidra/projects"))
PROJ_NAME = os.environ.get("GHIDRA_PROJ_NAME", "SkyrimScratch")

args = sys.argv[1:]
limit = 60
stop_on_jmp = False
addrs = []
i = 0
while i < len(args):
    if args[i] == "--n":
        limit = int(args[i + 1]); i += 2; continue
    if args[i] == "-j":
        stop_on_jmp = True; i += 1; continue
    addrs.append(int(args[i], 16)); i += 1
if not addrs:
    print("usage: disasm_at.py <hexaddr> [hexaddr ...] [--n N] [-j]", file=sys.stderr)
    sys.exit(2)

pyghidra.start()

from ghidra.base.project import GhidraProject             # noqa: E402
from ghidra.app.cmd.disassemble import DisassembleCommand  # noqa: E402
from ghidra.util.task import ConsoleTaskMonitor            # noqa: E402

gp = GhidraProject.openProject(PROJ_LOC, PROJ_NAME, True)
root = gp.getProject().getProjectData().getRootFolder()
prog = gp.openProgram("/", list(root.getFiles())[0].getName(), False)
space = prog.getAddressFactory().getDefaultAddressSpace()
listing = prog.getListing()
mon = ConsoleTaskMonitor()

for va in addrs:
    at = space.getAddress(va)
    print("\n==================== 0x%x ====================" % va)
    if listing.getInstructionAt(at) is None:
        txid = prog.startTransaction("disasm")
        try:
            DisassembleCommand(at, None, True).applyTo(prog, mon)
        finally:
            prog.endTransaction(txid, True)
    n = 0
    ins = listing.getInstructionAt(at)
    if ins is None:
        print("  !! no instruction decodes here")
        continue
    while ins is not None and n < limit:
        refs = [str(r.getToAddress()) for r in ins.getReferencesFrom()
                if ins.getMnemonicString().upper().startswith(("CALL", "JMP"))]
        print("  %s  %-44s %s" % (ins.getAddress(), ins, ("-> " + ", ".join(refs)) if refs else ""))
        n += 1
        m = ins.getMnemonicString().upper()
        if m.startswith("RET") or (stop_on_jmp and m == "JMP"):
            break
        ins = ins.getNext()

gp.close()
