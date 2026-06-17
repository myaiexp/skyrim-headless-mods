#!/usr/bin/env python3
# dump_updateimpl.py — standalone PyGhidra query over the analyzed SkyrimSE project.
#
# For each Projectile subclass, resolve its primary vtable (via MSVC RTTI symbols),
# read slot 0xAB (Projectile::UpdateImpl), and dump that function's decompilation +
# disassembly to disk. Surfaces a COMPACT call-site index to stdout so we can spot
# the internal layer-filtered world cast (the stream stop-point) without paging the
# whole function into context.
#
# Why: FlameProjectile/BeamProjectile compute their stop point inside the *non-virtual*
# body of UpdateImpl via a world cast that ignores systemGroup — no Address-Library seam.
# This dump tells us whether that cast is a hookable `call` (trampoline candidate) or
# fully inlined. Arrow/Missile/Cone are dumped too for diff.
#
# Run (after the background analyzeHeadless import+analysis has finished & released the lock):
#   GHIDRA_INSTALL_DIR=/opt/ghidra tools/ghidra/.venv/bin/python tools/ghidra/scripts/dump_updateimpl.py
#
# Output: tools/ghidra/out/<Class>_UpdateImpl.{c,asm} + a summary to stdout.

import os
import sys

import pyghidra

# Paths come from the ghidra.sh driver (GHIDRA_PROJ_*/GHIDRA_OUT); fall back to repo
# defaults so the script still runs if invoked directly.
_REPO = "/home/mse/Projects/skyrim-headless-mods"
PROJ_LOC = os.environ.get("GHIDRA_PROJ_LOC", os.path.join(_REPO, "tools/ghidra/projects"))
PROJ_NAME = os.environ.get("GHIDRA_PROJ_NAME", "SkyrimSE")
OUT = os.environ.get("GHIDRA_OUT", os.path.join(_REPO, "tools/ghidra/out"))

UPDATEIMPL_SLOT = 0xAB
PTR = 8
TARGETS = [
    "FlameProjectile",   # Flames — parked; no 0xBE; the prize
    "BeamProjectile",    # Sparks — parked; no 0xBE
    "ConeProjectile",    # cones — DOES expose 0xBE (feasible-later reference)
    "MissileProjectile", # aimed spells — pass-through WORKS (phantom stamp)
    "ArrowProjectile",   # arrows — pass-through WORKS (phantom stamp)
]

os.makedirs(OUT, exist_ok=True)
pyghidra.start()  # boots the JVM; uses GHIDRA_INSTALL_DIR

from ghidra.base.project import GhidraProject              # noqa: E402
from ghidra.app.decompiler import DecompInterface          # noqa: E402
from ghidra.util.task import ConsoleTaskMonitor            # noqa: E402

gp = GhidraProject.openProject(PROJ_LOC, PROJ_NAME, True)
root = gp.getProject().getProjectData().getRootFolder()
files = list(root.getFiles())
if not files:
    print("!! no programs in project root", file=sys.stderr)
    sys.exit(1)
prog_name = files[0].getName()
print("opening program: %s" % prog_name)
prog = gp.openProgram("/", prog_name, True)  # read-only

mem = prog.getMemory()
st = prog.getSymbolTable()
fm = prog.getFunctionManager()
space = prog.getAddressFactory().getDefaultAddressSpace()
listing = prog.getListing()

ifc = DecompInterface()
ifc.openProgram(prog)
mon = ConsoleTaskMonitor()


def addr(v):
    return space.getAddress(v & 0xFFFFFFFFFFFFFFFF)


def find_vftables(cls):
    out = []
    for sym in st.getAllSymbols(True):
        n = sym.getName(True)
        if cls in n and "vftable" in n.lower():
            out.append((n, sym.getAddress()))
    return out


def resolve_func(faddr):
    return fm.getFunctionAt(faddr) or fm.getFunctionContaining(faddr)


def call_index(func):
    rows = []
    seen = set()
    for ins in listing.getInstructions(func.getBody(), True):
        if not ins.getMnemonicString().upper().startswith("CALL"):
            continue
        tgt_name, tgt_addr = None, None
        for ref in ins.getReferencesFrom():
            ta = ref.getToAddress()
            if ta is None:
                continue
            tgt_addr = ta
            tf = fm.getFunctionAt(ta) or fm.getFunctionContaining(ta)
            if tf is not None:
                tgt_name = tf.getName()
            else:
                s = st.getPrimarySymbol(ta)
                tgt_name = s.getName() if s else "?"
            break
        key = (str(tgt_addr), tgt_name)
        if key in seen:
            continue
        seen.add(key)
        rows.append((str(ins.getAddress()), str(tgt_addr), tgt_name or "(indirect)"))
    return rows


print("=" * 72)
print("UpdateImpl (vtable slot 0x%X) dump" % UPDATEIMPL_SLOT)
print("=" * 72)

for cls in TARGETS:
    vfts = find_vftables(cls)
    print("\n### %s" % cls)
    if not vfts:
        print("  !! no vftable symbol found (RTTI miss?)")
        continue
    for n, a in vfts:
        print("  vft: %-52s @ %s" % (n, a))
    primary = None
    for n, a in vfts:
        if n.endswith("%s::vftable" % cls):
            primary = a
            break
    if primary is None:
        primary = vfts[0][1]

    slot_addr = primary.add(UPDATEIMPL_SLOT * PTR)
    func_va = mem.getLong(slot_addr)
    faddr = addr(func_va)
    func = resolve_func(faddr)
    print("  primary vft @ %s -> slot[0x%X] @ %s -> UpdateImpl @ %s"
          % (primary, UPDATEIMPL_SLOT, slot_addr, faddr))
    if func is None:
        print("  !! no function at UpdateImpl target (analysis miss)")
        continue

    cpath = os.path.join(OUT, "%s_UpdateImpl.c" % cls)
    apath = os.path.join(OUT, "%s_UpdateImpl.asm" % cls)
    res = ifc.decompileFunction(func, 180, mon)
    with open(cpath, "w") as fh:
        if res and res.decompileCompleted():
            fh.write(res.getDecompiledFunction().getC())
        else:
            fh.write("// decompile failed: %s\n" % (res.getErrorMessage() if res else "no result"))
    with open(apath, "w") as fh:
        for ins in listing.getInstructions(func.getBody(), True):
            fh.write("%s  %s\n" % (ins.getAddress(), ins))

    calls = call_index(func)
    ninsns = sum(1 for _ in listing.getInstructions(func.getBody(), True))
    print("  func %s  insns=%d  calls=%d  -> %s , %s"
          % (func.getName(), ninsns, len(calls),
             os.path.basename(cpath), os.path.basename(apath)))
    print("  CALL sites:")
    for at, ta, tn in calls:
        print("    %s  call %s  %s" % (at, ta, tn))

print("\nDONE. Full dumps in %s" % OUT)
gp.close()
