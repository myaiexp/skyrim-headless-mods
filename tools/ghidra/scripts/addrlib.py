#!/usr/bin/env python3
# addrlib.py — map Address Library IDs <-> exe offsets for a versionlib-*.bin (format 5).
#
# Bridges the two tiers: CommonLib addresses engine code by Address-Library ID, Ghidra by
# virtual address. `id` answers "where is REL::ID(67626) in THIS exe?" so decompile_at.py
# can be pointed at it; `offset` answers the reverse — "does the function Ghidra found have
# an ID I can hook through the library?" (a hit means the DLL can call it version-
# independently; a miss means it needs a vtable or a pattern).
#
#   addrlib.py id 67626 236556              # -> offset + VA (image base 0x140000000)
#   addrlib.py offset 0xcd5410 0x1a21110    # -> ID(s) mapping to that offset (VA accepted too)
#   ADDRLIB=/path/to/versionlib-1-7-104-0.bin addrlib.py …   # default: the test profile's 1.7.104
#
# Format 5 (1.7.99+, Address Library "All in One" v13) is a dense table: a 96-byte header
# then uint32 offset[id] for every id up to offsetCount, 0 = unassigned. Formats 1/2
# (delta-encoded pairs, 1.6.x and earlier) are NOT handled — CommonLib's REL/IDDB.cpp has
# that decoder if it's ever needed here. Pure Python, no Ghidra, no venv.

import os
import struct
import sys

IMAGE_BASE = 0x140000000
GAME = os.environ.get(
    "GAME_ROOT",
    os.path.expanduser("~/.steam/steam/steamapps/common/Skyrim Special Edition"),
)
DEFAULT = os.path.join(GAME, ".profiles/test/SKSE/Plugins/versionlib-1-7-104-0.bin")
HEADER = struct.Struct("<i4I64siii")  # format, version[4], name[64], pointerSize, dataFormat, offsetCount


def load(path):
    with open(path, "rb") as fh:
        head = fh.read(HEADER.size)
        fmt, v0, v1, v2, v3, name, ptr, dfmt, count = HEADER.unpack(head)
        if fmt != 5:
            sys.exit("addrlib: %s is format %d; only format 5 (1.7.99+) is decoded here" % (path, fmt))
        table = fh.read(count * 4)
    if len(table) != count * 4:
        sys.exit("addrlib: %s truncated (%d of %d offsets)" % (path, len(table) // 4, count))
    offsets = struct.unpack("<%dI" % count, table)
    ver = "%d.%d.%d.%d" % (v0, v1, v2, v3)
    return ver, name.split(b"\0", 1)[0].decode("ascii", "replace"), offsets


def main(argv):
    if len(argv) < 2 or argv[0] not in ("id", "offset"):
        print(__doc__ or "usage: addrlib.py id|offset <value>...", file=sys.stderr)
        return 2
    path = os.environ.get("ADDRLIB", DEFAULT)
    ver, name, offsets = load(path)
    print("# %s  game %s  (%s)  %d ids" % (os.path.basename(path), ver, name, len(offsets)))
    if argv[0] == "id":
        for a in argv[1:]:
            i = int(a, 0)
            off = offsets[i] if i < len(offsets) else 0
            if off:
                print("id %-8d offset 0x%-8x VA 0x%x" % (i, off, IMAGE_BASE + off))
            else:
                print("id %-8d (unassigned)" % i)
        return 0
    # offset -> id(s): accept a VA or a bare offset
    want = set()
    for a in argv[1:]:
        v = int(a, 0)
        want.add(v - IMAGE_BASE if v >= IMAGE_BASE else v)
    hits = {}
    for i, off in enumerate(offsets):
        if off in want:
            hits.setdefault(off, []).append(i)
    for off in sorted(want):
        ids = hits.get(off)
        if ids:
            print("offset 0x%-8x VA 0x%x  id %s" % (off, IMAGE_BASE + off, ", ".join(str(i) for i in ids)))
        else:
            print("offset 0x%-8x VA 0x%x  (no id — not in the library)" % (off, IMAGE_BASE + off))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
