#!/usr/bin/env python3
# find_via_rtti.py — get to UpdateImpl WITHOUT waiting for full auto-analysis.
#
# pyghidra.open_program(analyze=False) imports the PE in seconds (no analysis). We then
# walk MSVC RTTI by hand to find each projectile subclass's vtable, read slot 0xAB
# (Projectile::UpdateImpl), disassemble it on demand, and dump its call sites — the
# "is there a hookable cast" question, answered directly.
#
# RTTI walk (x64 MSVC):
#   TypeDescriptor:   [+0x00 type_info vftable][+0x08 spare][+0x10 ".?AV<Class>@@\0"]
#   COMPLETE_OBJECT_LOCATOR: [+0x0C pTypeDescriptor RVA][+0x14 pSelf RVA]   (sig==1 for x64)
#   vftable: the slot AFTER an 8-byte pointer to the COL  (vftable[-1] == &COL)
#
# Run: GHIDRA_INSTALL_DIR=/opt/ghidra tools/ghidra/.venv/bin/python tools/ghidra/scripts/find_via_rtti.py
#   (the ghidra.sh driver sets GHIDRA_* for you, but this uses a SEPARATE scratch project
#    so it can run while the main analysis is still going.)

import os

import pyghidra

REPO = "/home/mse/Projects/skyrim-headless-mods"
EXE = os.environ.get(
    "GHIDRA_BINARY",
    "/home/mse/.local/share/Steam/steamapps/common/Skyrim Special Edition/SkyrimSE.exe",
)
SCRATCH_LOC = os.path.join(REPO, "tools/ghidra/projects")
# Tagged with the unpacked exe's md5 by ghidra.sh (GHIDRA_SCRATCH_NAME), so a re-unpacked
# exe imports into a FRESH project: open_program() reuses an existing program of the same
# name without re-importing, which is how a June project kept answering for a September exe.
SCRATCH_NAME = os.environ.get("GHIDRA_SCRATCH_NAME", "SkyrimScratch")
OUT = os.environ.get("GHIDRA_OUT", os.path.join(REPO, "tools/ghidra/out"))

UPDATEIMPL_SLOT = 0xAB
TARGETS = ["FlameProjectile", "BeamProjectile", "ConeProjectile",
           "MissileProjectile", "ArrowProjectile"]

# Generalised in 2026-09 for the DBVO volume-boost RE: any class, any vfunc slot(s).
#   find_via_rtti.py                                  -> the original projectile UpdateImpl sweep
#   find_via_rtti.py BSXAudio2GameSound:0x18,0x0E     -> that class, those slots
#   find_via_rtti.py Foo Bar:0x05                     -> Foo at the default slot, Bar at 0x05
# Each (class, slot) pair dumps <Class>_slot0x<N>.{asm,c} (the default sweep keeps its
# historical <Class>_UpdateImpl names).
import sys

def _parse_targets(argv):
    if not argv:
        return [(cls, UPDATEIMPL_SLOT, "UpdateImpl") for cls in TARGETS]
    out = []
    for arg in argv:
        cls, _, slots = arg.partition(":")
        for s in (slots.split(",") if slots else [hex(UPDATEIMPL_SLOT)]):
            n = int(s, 0)
            out.append((cls, n, "slot0x%02X" % n))
    return out

JOBS = _parse_targets(sys.argv[1:])

os.makedirs(OUT, exist_ok=True)
pyghidra.start()

import jpype                                               # noqa: E402
from ghidra.app.decompiler import DecompInterface          # noqa: E402
from ghidra.app.cmd.disassemble import DisassembleCommand  # noqa: E402
from ghidra.app.cmd.function import CreateFunctionCmd      # noqa: E402
from ghidra.util.task import ConsoleTaskMonitor            # noqa: E402


def jbytes(bs):
    arr = jpype.JArray(jpype.JByte)(len(bs))
    for i, b in enumerate(bs):
        arr[i] = b - 256 if b > 127 else b
    return arr


with pyghidra.open_program(EXE, project_location=SCRATCH_LOC, project_name=SCRATCH_NAME,
                           analyze=False, nested_project_location=False) as flat:
    prog = flat.getCurrentProgram()
    mem = prog.getMemory()
    space = prog.getAddressFactory().getDefaultAddressSpace()
    listing = prog.getListing()
    fm = prog.getFunctionManager()
    base = prog.getImageBase().getOffset()
    mon = ConsoleTaskMonitor()
    minA = prog.getMinAddress()

    def A(v):
        return space.getAddress(v & 0xFFFFFFFFFFFFFFFF)

    def find_all(pattern, limit=64):
        """All addresses whose bytes == pattern (searches initialized memory)."""
        out, start = [], minA
        jb = jbytes(pattern)
        while len(out) < limit:
            try:
                hit = mem.findBytes(start, jb, None, True, mon)
            except Exception:
                hit = None
            if hit is None:
                break
            out.append(hit)
            start = hit.add(1)
        return out

    def le(n, width):
        return [(n >> (8 * i)) & 0xFF for i in range(width)]

    ifc = DecompInterface()
    ifc.openProgram(prog)

    print("image base = 0x%x" % base)

    for cls, SLOT, TAG in JOBS:
        print("\n### %s slot 0x%X" % (cls, SLOT))
        name = (".?AV%s@@" % cls).encode("ascii")
        td_names = find_all(list(name))
        if not td_names:
            print("  !! type-descriptor string not found")
            continue
        # TypeDescriptor starts 0x10 before its name field.
        vftables = []
        for saddr in td_names:
            td = saddr.subtract(0x10) if hasattr(saddr, "subtract") else A(saddr.getOffset() - 0x10)
            td_rva = td.getOffset() - base
            # COLs referencing this TD: 4-byte RVA appears at COL+0x0C.
            for col_rva_hit in find_all(le(td_rva, 4)):
                col = A(col_rva_hit.getOffset() - 0x0C)
                try:
                    if mem.getInt(col) != 1:        # signature: 1 == x64
                        continue
                    if mem.getInt(col.add(0x0C)) != td_rva:
                        continue
                except Exception:
                    continue
                col_off = col.getOffset()
                # vftable[-1] holds an 8-byte pointer to the COL.
                for meta_hit in find_all(le(col_off, 8)):
                    vft = meta_hit.add(8)
                    vftables.append((col, vft, mem.getInt(col.add(0x04))))  # (col, vft, base-offset)

        if not vftables:
            print("  !! no vftable located via RTTI")
            continue
        # Prefer the primary subobject vtable (COL offset field == 0).
        vftables.sort(key=lambda t: t[2])
        for col, vft, off in vftables:
            print("  vft @ %s (COL %s, subobj-offset %d)" % (vft, col, off))
        vft = vftables[0][1]

        slot = vft.add(SLOT * 8)
        ui = A(mem.getLong(slot))
        print("  primary vft %s -> slot[0x%X] -> %s @ %s" % (vft, SLOT, TAG, ui))

        # Disassemble on demand (no auto-analysis ran), via the same commands the analyzer
        # uses. Write ops need a transaction.
        if listing.getInstructionAt(ui) is None:
            txid = prog.startTransaction("disasm %s" % cls)
            try:
                dok = DisassembleCommand(ui, None, True).applyTo(prog, mon)
                cok = CreateFunctionCmd(ui).applyTo(prog, mon)
                print("  (DisassembleCommand=%s, CreateFunctionCmd=%s)" % (dok, cok))
            finally:
                prog.endTransaction(txid, True)
        func = fm.getFunctionAt(ui) or fm.getFunctionContaining(ui)
        if func is None:
            print("  !! could not form a function at %s" % ui)
            continue

        apath = os.path.join(OUT, "%s_%s.asm" % (cls, TAG))
        with open(apath, "w") as fh:
            for ins in listing.getInstructions(func.getBody(), True):
                fh.write("%s  %s\n" % (ins.getAddress(), ins))
        cpath = os.path.join(OUT, "%s_%s.c" % (cls, TAG))
        res = ifc.decompileFunction(func, 180, mon)
        with open(cpath, "w") as fh:
            if res and res.decompileCompleted():
                fh.write(res.getDecompiledFunction().getC())
            else:
                fh.write("// decompile failed: %s\n" % (res.getErrorMessage() if res else "no result"))

        # Call-site index (the seam candidates).
        calls, seen = [], set()
        for ins in listing.getInstructions(func.getBody(), True):
            if not ins.getMnemonicString().upper().startswith("CALL"):
                continue
            ta = None
            for ref in ins.getReferencesFrom():
                ta = ref.getToAddress()
                break
            key = str(ins.getAddress())
            if key in seen:
                continue
            seen.add(key)
            calls.append((str(ins.getAddress()), str(ta) if ta else "(indirect)"))
        ninsns = sum(1 for _ in listing.getInstructions(func.getBody(), True))
        print("  func %s  insns=%d  calls=%d -> %s , %s"
              % (func.getName(), ninsns, len(calls), os.path.basename(apath), os.path.basename(cpath)))
        for at, ta in calls:
            print("    %s  call %s" % (at, ta))

    print("\nDONE.")
