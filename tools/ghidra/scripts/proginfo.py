#!/usr/bin/env python3
# proginfo.py — which exe does a Ghidra project actually hold? Plus raw bytes at addresses.
#
# The scratch and analysed projects are created once and then REUSED by every query —
# pyghidra.open_program() opens an existing program rather than re-importing — so after a
# game patch a project can silently keep serving the OLD exe's bytes while the Address
# Library on disk describes the new one. This prints the program's recorded executable
# path/MD5/format and image base so that can be checked against `md5sum` of the unpacked
# exe, and dumps 16 bytes at any addresses given (to compare with `xxd` on the file).
#
# Run: ghidra.sh query proginfo.py [0xaddr ...]
#      GHIDRA_PROJ_NAME=SkyrimSE ghidra.sh query proginfo.py 0x140cd5410

import os
import sys

import pyghidra

REPO = "/home/mse/Projects/skyrim-headless-mods"
PROJ_LOC = os.environ.get("GHIDRA_PROJ_LOC", os.path.join(REPO, "tools/ghidra/projects"))
PROJ_NAME = os.environ.get("GHIDRA_PROJ_NAME", "SkyrimScratch")

addrs = [int(a, 16) for a in sys.argv[1:]]

pyghidra.start()

from ghidra.base.project import GhidraProject  # noqa: E402

gp = GhidraProject.openProject(PROJ_LOC, PROJ_NAME, True)
root = gp.getProject().getProjectData().getRootFolder()
files = list(root.getFiles())
print("project %s at %s: %d program(s)" % (PROJ_NAME, PROJ_LOC, len(files)))
for f in files:
    print("  program: %s" % f.getName())
prog = gp.openProgram("/", files[0].getName(), True)
print("executable path : %s" % prog.getExecutablePath())
print("executable md5  : %s" % prog.getExecutableMD5())
print("executable fmt  : %s" % prog.getExecutableFormat())
print("image base      : %s" % prog.getImageBase())
print("creation date   : %s" % prog.getCreationDate())
mem = prog.getMemory()
for b in mem.getBlocks():
    print("  block %-10s %s-%s  %8d bytes  init=%s x=%s"
          % (b.getName(), b.getStart(), b.getEnd(), b.getSize(), b.isInitialized(), b.isExecute()))
space = prog.getAddressFactory().getDefaultAddressSpace()
for va in addrs:
    at = space.getAddress(va)
    try:
        bs = bytearray(16)
        n = mem.getBytes(at, bs)
        print("bytes @ 0x%x: %s" % (va, " ".join("%02x" % (x & 0xFF) for x in bs[:n])))
    except Exception as e:  # noqa: BLE001
        print("bytes @ 0x%x: unreadable (%s)" % (va, e))
gp.close()
