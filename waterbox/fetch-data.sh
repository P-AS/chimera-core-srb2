#!/bin/sh
# fetch-data.sh - the game's data for the gate where none is installed (CI).
#
# usage: waterbox/fetch-data.sh [<out folder>]   (default build/srb2-2.2.15)
#
# SRB2's data is Sonic Team Junior's, freely distributed by them and never
# carried by this repository: this downloads STJr's own 2.2.15 release
# (SRB2-v2215-Full.zip, from github.com/STJr/SRB2's releases), takes the four
# pk3s the core declares as firmware out of it, and checks each against
# waterbox.config's SHA-1 - so the gate runs on exactly the files the package
# declares. A folder that already holds them, checked, is left as it is.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
out="${1:-$root/build/srb2-2.2.15}"
url="https://github.com/STJr/SRB2/releases/download/SRB2_release_2.2.15/SRB2-v2215-Full.zip"

check() {
	python3 - "$here/waterbox.config" "$out" <<'PY'
import hashlib, json, os, sys
cfg, out = json.load(open(sys.argv[1])), sys.argv[2]
bad = []
for fw in cfg["firmware"]:
    path = os.path.join(out, fw["name"])
    if not os.path.exists(path):
        bad.append(f"{fw['name']}: missing")
        continue
    h = hashlib.sha1(open(path, "rb").read()).hexdigest()
    if h != fw["sha1"]:
        bad.append(f"{fw['name']}: sha1 {h}, declared {fw['sha1']}")
if bad:
    print("\n".join(bad), file=sys.stderr)
    sys.exit(1)
PY
}

if check 2>/dev/null; then
	echo "data: $out (already there, checked)"
	exit 0
fi
mkdir -p "$out"
zip="$out/SRB2-v2215-Full.zip"
[ -f "$zip" ] || curl -fsSL --retry 3 --retry-delay 5 -o "$zip.part" "$url"
[ -f "$zip" ] || mv "$zip.part" "$zip"
python3 - "$zip" "$out" "$here/waterbox.config" <<'PY'
import json, os, sys, zipfile
zip_path, out, cfg = sys.argv[1], sys.argv[2], json.load(open(sys.argv[3]))
wanted = {fw["name"].lower(): fw["name"] for fw in cfg["firmware"]}
with zipfile.ZipFile(zip_path) as z:
    for info in z.infolist():
        base = os.path.basename(info.filename).lower()
        if base in wanted:
            with open(os.path.join(out, wanted[base]), "wb") as f:
                f.write(z.read(info))
PY
rm -f "$zip"
check
echo "data: $out (SRB2 2.2.15, from STJr's release, checked)"
