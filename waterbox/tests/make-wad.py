#!/usr/bin/env python3
"""make-wad.py - a PWAD of the given lumps: make-wad.py OUT NAME=FILE...

The gate's way of handing SRB2 content it does not ship (a GME song, a
module) without carrying it: the lump comes from a file already at hand
(a submodule's test data), and the WAD is made at gate time."""
import struct
import sys

out, pairs = sys.argv[1], sys.argv[2:]
lumps = []
for pair in pairs:
    name, path = pair.split("=", 1)
    with open(path, "rb") as f:
        lumps.append((name.upper().encode()[:8], f.read()))
data = b"".join(d for _, d in lumps)
directory = b""
offset = 12
for name, d in lumps:
    directory += struct.pack("<ii8s", offset, len(d), name)
    offset += len(d)
with open(out, "wb") as f:
    f.write(b"PWAD" + struct.pack("<ii", len(lumps), 12 + len(data)) + data + directory)
