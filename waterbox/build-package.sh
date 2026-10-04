#!/bin/sh
# Builds the SRB2 core package, srb2.chimeraCore.
#
# A package is core.wbx (fixed name) + waterbox.config + default_keybinds.json +
# file_slots.json + the licences + build.json, loaded through Chimera's one
# built-in generic adapter. It carries none of the game's data: srb2.pk3,
# zones.pk3, characters.pk3 and music.pk3 are the project's firmware.
#
# Usage: ./build-package.sh [-m <miniBox dir>] [-r <chimera root or bundle>] [-o <out dir>]
#   -m  miniBox, built with its C++ guest toolchain (default $MINIBOX_DIR, else
#       the -r checkout's extern/chimera-common-minibox)
#   -r  installs the package into a Chimera checkout's build/Cores (what CI's
#       publish job does), or a Chimera bundle's Cores (a folder with
#       Chimera.exe); its compiled-core cache for this package is cleared
#   -o  writes the package to <out dir> instead. Neither: build/package.
set -eu
here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
mb="${MINIBOX_DIR:-}"
chimera_root=""
out=""
while getopts "m:r:o:" opt; do
	case "$opt" in
		m) mb="$OPTARG" ;;
		r) chimera_root="$OPTARG" ;;
		o) out="$OPTARG" ;;
		*) sed -n '2,15p' "$0" >&2; exit 2 ;;
	esac
done

if [ -n "$chimera_root" ]; then
	[ -d "$chimera_root" ] || { echo "no Chimera at $chimera_root" >&2; exit 1; }
	chimera_root="$(cd "$chimera_root" && pwd)"
	if [ -f "$chimera_root/Chimera.exe" ]; then
		# a bundle: its cores sit beside the executable
		[ -n "$out" ] || out="$chimera_root/Cores"
	else
		[ -n "$mb" ] || mb="$chimera_root/extern/chimera-common-minibox"
		[ -n "$out" ] || out="$chimera_root/build/Cores"
	fi
fi
[ -n "$mb" ] || { echo "name miniBox: -m <path> or MINIBOX_DIR (built with -Dguest_cpp=true)" >&2; exit 1; }
[ -d "$mb" ] || { echo "miniBox not found at $mb" >&2; exit 1; }
mb="$(cd "$mb" && pwd)"
[ -n "$out" ] || out="$root/build/package"

# the guest (guest.mk runs check-wbx before it calls core.wbx built)
mkdir -p "$root/build"
make -C "$here" -f guest.mk MB="$mb" -j"$(nproc)" > "$root/build/package-make.log" 2>&1 || {
	tail -20 "$root/build/package-make.log" >&2; echo "the guest build failed (build/package-make.log)" >&2; exit 1; }
sh "$mb/source/guest/check-wbx.sh" "$root/build/guest/core.wbx"

# the declaration and the controller must agree before anything ships
python3 - "$here/waterbox.config" "$here/default_keybinds.json" <<'PYCHECK'
import json, sys
cfg = json.load(open(sys.argv[1]))
binds = json.load(open(sys.argv[2]))["AllTrollers"][cfg["input"]["name"]]
missing = [b for b in cfg["input"]["buttons"] if b not in binds]
extra = [b for b in binds if b not in cfg["input"]["buttons"]]
if missing or extra:
    sys.exit(f"default_keybinds.json does not match the declared buttons: missing {missing}, extra {extra}")
PYCHECK

staging="$root/build/package-staging"
rm -rf "$staging"
mkdir -p "$staging"
cp "$root/build/guest/core.wbx" "$staging/core.wbx"
cp "$here/waterbox.config" "$staging/waterbox.config"
cp "$here/default_keybinds.json" "$staging/default_keybinds.json"
cp "$here/file_slots.json" "$staging/file_slots.json"
# the terms travel with the binary (waterbox/package-licenses.json)
python3 "$mb/source/guest/package-licenses.py" "$root" "$staging"

# ---- version: the commit, and the commit's date in UTC (never the build's) ----
core_version="${CORE_VERSION:-}"
if [ -z "$core_version" ]; then
	if commit="$(git -C "$root" rev-parse --short=12 HEAD 2>/dev/null)"; then
		# the patch series inside extern/ is not an edit of this repository
		git -C "$root" diff --quiet --ignore-submodules=dirty HEAD 2>/dev/null || commit="$commit-dirty"
		core_version="$commit+local"
	else
		core_version="unversioned+local"
	fi
fi
core_version_date="$(TZ=UTC git -C "$root" log -1 --date=format-local:%Y-%m-%dT%H:%M:%SZ --format=%cd HEAD 2>/dev/null || true)"
python3 - "$staging/waterbox.config" "$core_version" "$core_version_date" <<'PYVER'
import json, sys
path, version, date = sys.argv[1], sys.argv[2], sys.argv[3]
cfg = json.load(open(path))
cfg["version"] = version
if date:
    cfg["versionDate"] = date
with open(path, "w") as f:
    json.dump(cfg, f, indent=2, ensure_ascii=False)
    f.write("\n")
PYVER

# ---- provenance: what built this exact package (inputs only) ----
python3 - "$root" "$mb" "$staging/build.json" "$core_version" <<'PYPROV'
import json, subprocess, sys
root, mb, path, version = sys.argv[1:5]
def run(*args, cwd=None, default="unknown"):
    try:
        return subprocess.run(list(args), cwd=cwd, capture_output=True, text=True, check=True).stdout.strip()
    except Exception:
        return default
def git(where, *args, default="unknown"):
    return run("git", "-C", where, *args, default=default)
os_id = run("sh", "-c", ". /etc/os-release && printf '%s %s' \"$ID\" \"${VERSION_ID:-}\"")
json.dump({
    "version": version,
    "source": {"commit": git(root, "rev-parse", "HEAD"),
               "origin": git(root, "config", "--get", "remote.origin.url", default=""),
               "dirty": "-dirty" in version},
    "toolchain": {"compiler": "gcc " + run("gcc", "-dumpfullversion"),
                  "binutils": (run("ld", "--version").splitlines() or ["unknown"])[0].split()[-1],
                  "target": "x86_64-linux-musl",
                  "musl": open(mb + "/extern/musl/VERSION").read().strip()},
    "guestKit": {"name": "miniBox", "commit": git(mb, "rev-parse", "--short=12", "HEAD")},
    "upstream": {name: git(root + "/extern/" + name, "describe", "--tags", "--always")
                 for name in ("SRB2", "ogg", "vorbis", "gme", "openmpt")},
    "builtOn": os_id,
}, open(path, "w"), indent=2, sort_keys=True)
PYPROV

mkdir -p "$out"
zip_path="$out/srb2.chimeraCore"
rm -f "$zip_path"
# deterministic packaging: sorted entries, fixed timestamp and permissions,
# pinned compression - the package's SHA1 is the core's identity (movies cite it)
python3 - "$staging" "$zip_path" <<'PYEOF'
import hashlib, os, sys, tempfile, zipfile

staging, zip_path = sys.argv[1], sys.argv[2]
FIXED_DATE = (1980, 1, 1, 0, 0, 0)

def write_package(path):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for root, dirs, files in os.walk(staging):
            dirs.sort()
            for name in sorted(files):
                full = os.path.join(root, name)
                info = zipfile.ZipInfo(os.path.relpath(full, staging), date_time=FIXED_DATE)
                info.compress_type = zipfile.ZIP_DEFLATED
                info.create_system = 3
                info.external_attr = 0o644 << 16
                with open(full, "rb") as f:
                    z.writestr(info, f.read())

write_package(zip_path)
with tempfile.NamedTemporaryFile(suffix=".zip") as tmp:
    write_package(tmp.name)
    again = hashlib.sha1(open(tmp.name, "rb").read()).hexdigest()
first = hashlib.sha1(open(zip_path, "rb").read()).hexdigest()
if first != again:
    sys.exit(f"packaging is not deterministic: {first} then {again}")
print(f"package sha1 {first}")
PYEOF

if [ -n "$chimera_root" ]; then
	for cache in "$chimera_root"/build/CoreCache/srb2-* "$chimera_root"/CoreCache/srb2-*; do
		[ -d "$cache" ] && rm -rf "$cache" || true
	done
fi
echo "packaged -> $zip_path"
