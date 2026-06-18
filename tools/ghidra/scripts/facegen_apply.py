#!/usr/bin/env python3
# facegen_apply.py — find the morph-APPLY/read seam for the dialogue mouth-snap fix.
#
# The visible NPC mouth is BSFaceGenAnimationData::transitionTargetKeyFrame (a
# BSFaceGenKeyframeMultiple). The audio-driven lip pump writes kf->values[] each frame;
# SOMETHING reads them and deforms the head mesh (the "apply"). A vtable detour on
# BSFaceGenNiNode::UpdateDownwardPass (0x2C) scales the keyframe but renders too late — wrong
# seam. We need the actual read site so a hook there scales what the SAME frame's mesh deform
# reads.
#
# Two candidate locations this script dumps:
#   1. BSFaceGenKeyframeMultiple's OWN vtable — if one slot iterates this->values (+0x10) and
#      checks isUpdated (+0x1C), the apply is a keyframe virtual => a clean CommonLib VTABLE
#      hook (patch-portable, no trampoline). Best case.
#   2. BSFaceGenNiNode::UpdateDownwardPass (slot 0x2C) body + its call-sites — to see what the
#      facegen node actually does per frame and which sub-call is the morph apply.
#
# Same RTTI-walk as find_via_rtti.py (analyze=False, scratch project, disassemble on demand).
# Run: GHIDRA_INSTALL_DIR=/opt/ghidra GHIDRA_BINARY=<unpacked> \
#        tools/ghidra/.venv/bin/python tools/ghidra/scripts/facegen_apply.py

import os
import re

import pyghidra

REPO = "/home/mse/Projects/skyrim-headless-mods"
EXE = os.environ.get(
    "GHIDRA_BINARY",
    os.path.join(REPO, "tools/ghidra/.steamless/SkyrimSE.exe.unpacked.exe"),
)
SCRATCH_LOC = os.path.join(REPO, "tools/ghidra/projects")
SCRATCH_NAME = "SkyrimScratch"
OUT = os.environ.get("GHIDRA_OUT", os.path.join(REPO, "tools/ghidra/out"))

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
    maxA = prog.getMaxAddress()

    def A(v):
        return space.getAddress(v & 0xFFFFFFFFFFFFFFFF)

    def find_all(pattern, limit=64):
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

    def find_vftables(cls):
        """Return [(col, vft_addr, subobj_off)] for class `cls`, primary subobj first."""
        name = (".?AV%s@@" % cls).encode("ascii")
        td_names = find_all(list(name))
        vftables = []
        for saddr in td_names:
            td = saddr.subtract(0x10)
            td_rva = td.getOffset() - base
            for col_rva_hit in find_all(le(td_rva, 4)):
                col = A(col_rva_hit.getOffset() - 0x0C)
                try:
                    if mem.getInt(col) != 1:
                        continue
                    if mem.getInt(col.add(0x0C)) != td_rva:
                        continue
                except Exception:
                    continue
                col_off = col.getOffset()
                for meta_hit in find_all(le(col_off, 8)):
                    vft = meta_hit.add(8)
                    vftables.append((col, vft, mem.getInt(col.add(0x04))))
        vftables.sort(key=lambda t: t[2])
        return vftables

    def in_text(addr):
        try:
            blk = mem.getBlock(addr)
            return blk is not None and blk.isExecute()
        except Exception:
            return False

    def dump_func(at, tag):
        """Disassemble + decompile function at `at`; write .asm/.c; return summary dict."""
        if listing.getInstructionAt(at) is None:
            txid = prog.startTransaction("disasm %s" % tag)
            try:
                DisassembleCommand(at, None, True).applyTo(prog, mon)
                CreateFunctionCmd(at).applyTo(prog, mon)
            finally:
                prog.endTransaction(txid, True)
        func = fm.getFunctionAt(at) or fm.getFunctionContaining(at)
        if func is None:
            return {"ok": False, "tag": tag, "at": str(at)}
        # asm dump
        apath = os.path.join(OUT, "%s.asm" % tag)
        with open(apath, "w") as fh:
            for ins in listing.getInstructions(func.getBody(), True):
                fh.write("%s  %s\n" % (ins.getAddress(), ins))
        # decompile
        cpath = os.path.join(OUT, "%s.c" % tag)
        ctext = ""
        res = ifc.decompileFunction(func, 180, mon)
        if res and res.decompileCompleted():
            ctext = res.getDecompiledFunction().getC()
        with open(cpath, "w") as fh:
            fh.write(ctext or "// decompile failed\n")
        # call-site index
        calls = []
        for ins in listing.getInstructions(func.getBody(), True):
            if not ins.getMnemonicString().upper().startswith("CALL"):
                continue
            ta = None
            for ref in ins.getReferencesFrom():
                ta = ref.getToAddress()
                break
            calls.append((str(ins.getAddress()), str(ta) if ta else "(indirect)"))
        # heuristic flags over the decompiled C
        flags = []
        if re.search(r"\+\s*0x10\b", ctext):
            flags.append("reads+0x10(values?)")
        if re.search(r"\+\s*0x1[8c]\b", ctext):
            flags.append("reads+0x18/1c(count/isUpdated?)")
        if re.search(r"\b(for|while)\s*\(", ctext) or re.search(r"\bdo\s*\{", ctext):
            flags.append("LOOP")
        ninsns = sum(1 for _ in listing.getInstructions(func.getBody(), True))
        return {"ok": True, "tag": tag, "at": str(at), "name": func.getName(),
                "insns": ninsns, "ncalls": len(calls), "calls": calls, "flags": flags}

    print("image base = 0x%x" % base)

    # ---- 1. BSFaceGenKeyframeMultiple vtable: dump every slot ---------------------
    print("\n### BSFaceGenKeyframeMultiple vtable")
    vfs = find_vftables("BSFaceGenKeyframeMultiple")
    if not vfs:
        print("  !! vtable not found via RTTI")
    else:
        for col, vft, off in vfs:
            print("  vft @ %s (COL %s, subobj-offset %d)" % (vft, col, off))
        vft = vfs[0][1]
        for slot in range(0x00, 0x14):
            sa = vft.add(slot * 8)
            try:
                fnaddr = A(mem.getLong(sa))
            except Exception:
                break
            if not in_text(fnaddr):
                print("  slot[0x%02X] -> %s (not in .text — vtable end)" % (slot, fnaddr))
                break
            s = dump_func(fnaddr, "kfm_slot_%02X" % slot)
            if not s["ok"]:
                print("  slot[0x%02X] @ %s  (no function)" % (slot, fnaddr))
                continue
            print("  slot[0x%02X] @ %s  insns=%d calls=%d  %s  [%s]"
                  % (slot, fnaddr, s["insns"], s["ncalls"],
                     " ".join(s["flags"]) or "-", s["tag"]))

    # ---- 2. BSFaceGenNiNode::UpdateDownwardPass (slot 0x2C) + call-sites ----------
    print("\n### BSFaceGenNiNode::UpdateDownwardPass (slot 0x2C)")
    vfs = find_vftables("BSFaceGenNiNode")
    if not vfs:
        print("  !! vtable not found via RTTI")
    else:
        vft = vfs[0][1]
        sa = vft.add(0x2C * 8)
        fnaddr = A(mem.getLong(sa))
        print("  vft @ %s  slot[0x2C] -> %s" % (vft, fnaddr))
        s = dump_func(fnaddr, "facegen_ninode_UpdateDownwardPass")
        if s["ok"]:
            print("  %s  insns=%d calls=%d  [%s]" % (s["name"], s["insns"], s["ncalls"], s["tag"]))
            for at, ta in s["calls"]:
                print("    %s  call %s" % (at, ta))

    print("\nDONE.  full dumps in %s" % OUT)
