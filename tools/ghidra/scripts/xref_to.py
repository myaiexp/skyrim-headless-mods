#!/usr/bin/env python3
# xref_to.py — list functions that reference each given address (analyzed project).
# Use: ghidra.sh query xref_to.py 0x14313fa18 0x14313fa28 ...
# Prints, per target, the containing function of every reference-to site (read/write/call).
# For data globals this surfaces producers vs consumers; for code it surfaces callers.

import os
import sys

import pyghidra

pyghidra.start()
from ghidra.base.project import GhidraProject  # noqa: E402

targets = [int(a, 16) for a in sys.argv[1:]]
if not targets:
    print("usage: xref_to.py <hexaddr> [hexaddr ...]", file=sys.stderr)
    raise SystemExit(2)

gp = GhidraProject.openProject(os.environ["GHIDRA_PROJ_LOC"], os.environ["GHIDRA_PROJ_NAME"], True)
root = gp.getProject().getProjectData().getRootFolder()
prog = gp.openProgram("/", list(root.getFiles())[0].getName(), True)
try:
    space = prog.getAddressFactory().getDefaultAddressSpace()
    rm = prog.getReferenceManager()
    fm = prog.getFunctionManager()
    for t in targets:
        ta = space.getAddress(t)
        print("\n==================== xrefs -> 0x%x ====================" % t)
        seen = set()
        for ref in rm.getReferencesTo(ta):
            frm = ref.getFromAddress()
            fn = fm.getFunctionContaining(frm)
            rt = ref.getReferenceType()
            key = (str(fn.getEntryPoint()) if fn else str(frm))
            tag = "%s @ %s  [%s]" % (fn.getName() if fn else "(no func)", frm, rt)
            if key in seen:
                continue
            seen.add(key)
            print("  " + tag)
finally:
    gp.close()
