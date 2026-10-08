#!/usr/bin/env bash
# Builds the OpenGL SRB2's OpenGL renderer draws through inside the sandbox:
# Mesa's softpipe driver behind the gallium OSMesa front end, static, no LLVM,
# cross-built for the miniBox guest. softpipe is plain C - no JIT, no dispatch
# on host CPU features - so it draws the same picture on every machine.
#
# Taken from chimera-core-flycast's waterbox/setup-mesa.sh: the same Mesa
# (24.0.9, pinned by SHA256) and options, plus three host-detected features
# pinned off and a release build (below). Its licence:
#
#   MIT License
#
#   Copyright (c) 2026 Sergio Martin
#
#   Permission is hereby granted, free of charge, to any person obtaining a copy
#   of this software and associated documentation files (the "Software"), to deal
#   in the Software without restriction, including without limitation the rights
#   to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
#   copies of the Software, and to permit persons to whom the Software is
#   furnished to do so, subject to the following conditions:
#
#   The above copyright notice and this permission notice shall be included in all
#   copies or substantial portions of the Software.
#
#   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
#   IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
#   FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
#   AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
#   LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
#   OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
#   SOFTWARE.
#
# Produces:  build/mesa/build-guest2/  (the *.a archives plus the gallium osmesa
# target.c.o that guest.mk links).
#
# Usage: ./setup-mesa.sh [-m <miniBox dir>] [-j N]   (default $MINIBOX_DIR)
#   MESA_TARBALL=<path>            use a tarball already on this machine
#   MESA_BUILD_CONFIGURE_ONLY=1    validate the recipe without the ~15min compile
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/.." && pwd)"
minibox="${MINIBOX_DIR:-}"
jobs="$(nproc)"
while getopts "m:j:" opt; do
	case "$opt" in
		m) minibox="$OPTARG" ;;
		j) jobs="$OPTARG" ;;
		*) exit 2 ;;
	esac
done
[ -n "$minibox" ] || { echo "name miniBox: -m <path> or MINIBOX_DIR" >&2; exit 1; }
minibox="$(cd "$minibox" && pwd)"

# The guest sysroot must be the C++ (meson-cpp) one: it carries libstdc++ and
# the C++ headers the C-only meson-linux sysroot lacks.
sr="$minibox/build/meson-cpp/guest-sysroot"
[ -f "$sr/lib/musl-gcc.specs" ] || { echo "guest sysroot not built at $sr (build miniBox's meson-cpp first)" >&2; exit 1; }
gccver="$(basename "$(ls -d "$sr"/include/c++/* | head -1)")"

version="24.0.9"
sha256="51aa686ca4060e38711a9e8f60c8f1efaa516baf411946ed7f2c265cd582ca4c"
url="https://archive.mesa3d.org/mesa-$version.tar.xz"

mesa="$root/build/mesa"
build="$mesa/build-guest2"
cache="${CHIMERA_DEPS_DIR:-$root/build/deps}"
tarball="${MESA_TARBALL:-$cache/mesa-$version.tar.xz}"

if [ -d "$build" ] && find "$build" -name '*.a' -print -quit 2>/dev/null | grep -q . \
	&& find "$build/src/gallium/targets/osmesa" -name 'target.c.o' -print -quit 2>/dev/null | grep -q .; then
	echo "mesa: already built in $build"
	exit 0
fi

if [ ! -f "$tarball" ]; then
	mkdir -p "$cache"
	echo "mesa: fetching $url"
	curl -fL --retry 3 -o "$tarball.part" "$url"
	mv "$tarball.part" "$tarball"
fi
echo "$sha256  $tarball" | sha256sum -c - >/dev/null || {
	echo "mesa: the tarball is not the release this core is pinned to" >&2
	exit 1
}

if [ ! -d "$mesa/src/gallium" ]; then
	rm -rf "$mesa"
	mkdir -p "$mesa"
	tar -xf "$tarball" -C "$mesa" --strip-components=1
fi

# meson and its deps in a private venv, so the host Python is left alone. Mesa's
# version check needs `packaging` (3.12 dropped distutils); mako generates the
# GL dispatch.
# Prefer a system meson that already has what Mesa needs; fall back to a
# private venv. Not the other way round: `python3 -m venv` fails outright on a
# machine without python3-venv, and there is no reason to need it when the host
# can already generate Mesa's GL dispatch.
meson=""
if command -v meson >/dev/null && python3 -c "import mako, packaging" 2>/dev/null; then
	meson="$(command -v meson)"
else
	venv="${MESA_BUILD_VENV:-$HOME/.cache/chimera-mesa-build-venv}"
	if [ ! -x "$venv/bin/meson" ]; then
		python3 -m venv "$venv" || {
			echo "mesa: no usable meson - install python3-venv, or meson plus python3-mako" >&2
			exit 1
		}
		"$venv/bin/pip" -q install --upgrade pip
		"$venv/bin/pip" -q install meson ninja mako packaging
	fi
	meson="$venv/bin/meson"
fi

# single-`-specs` wrapper compilers: passing -specs through meson's *_args
# doubles it and the specs file errors, so the flag rides inside the compiler.
cat > "$mesa/gw-cc"  <<EOF
#!/bin/sh
exec gcc "\$@" -specs $sr/lib/musl-gcc.specs
EOF
cat > "$mesa/gw-cxx" <<EOF
#!/bin/sh
exec g++ "\$@" -specs $sr/lib/musl-gcc.specs
EOF
chmod +x "$mesa/gw-cc" "$mesa/gw-cxx"

# Kernel uapi headers, as a LAST-RESORT include path. The guest compiles with
# -nostdinc plus the musl sysroot (from the specs), and musl ships no linux/,
# asm/ or asm-generic/ headers - but mesa's include/drm-uapi/drm.h includes
# <linux/types.h>, so src/mesa/main/externalobjects.c cannot compile without
# them. The directory below contains nothing but symlinks to those three
# kernel trees, and it is passed with -idirafter, which is searched AFTER the
# sysroot: musl still wins for every header it provides, and only the kernel
# uapi ones fall through. A plain -I/usr/include would let glibc headers
# shadow musl's, which is exactly what -nostdinc is here to prevent.
uapi="$mesa/uapi-include"
mkdir -p "$uapi"
ln -sfn /usr/include/linux "$uapi/linux"
ln -sfn /usr/include/asm-generic "$uapi/asm-generic"
if [ -d /usr/include/x86_64-linux-gnu/asm ]; then
	ln -sfn /usr/include/x86_64-linux-gnu/asm "$uapi/asm"
elif [ -d /usr/include/asm ]; then
	ln -sfn /usr/include/asm "$uapi/asm"
fi

# large code model, static reloc, no %fs stack guard, the guest's own libstdc++.
# On aarch64, miniBox's aarch64 machine instead: the small code model (any
# base works there), no return-address signing, no outline atomics, and
# x86-64's arithmetic - no fused multiply-add, signed char (guest.mk's WBCPU).
case "$(uname -m)" in
aarch64)
	cpu_family=aarch64
	triplet=aarch64-linux-musl
	cpu_args="'-mbranch-protection=none', '-mno-outline-atomics', '-ffp-contract=off', '-fsigned-char'" ;;
*)
	cpu_family=x86_64
	triplet=x86_64-linux-musl
	cpu_args="'-mcmodel=large', '-fcf-protection=none'" ;;
esac
cat > "$mesa/guest-cross.ini" <<EOF
[binaries]
c = '$mesa/gw-cc'
cpp = '$mesa/gw-cxx'
ar = 'ar'
strip = 'strip'
pkg-config = 'pkg-config'

[host_machine]
system = 'linux'
cpu_family = '$cpu_family'
cpu = '$cpu_family'
endian = 'little'

[properties]
needs_exe_wrapper = true

[built-in options]
c_args = [$cpu_args, '-mstack-protector-guard=global', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-idirafter', '$uapi']
cpp_args = [$cpu_args, '-mstack-protector-guard=global', '-fno-stack-protector', '-fno-pic', '-fno-pie', '-fexceptions', '-I$sr/include/c++/$gccver', '-I$sr/include/c++/$gccver/$triplet', '-idirafter', '$uapi']
EOF

# softpipe + gallium OSMesa, static, no LLVM, nothing that pulls a host lib.
# lmsensors, libunwind and valgrind are auto features Mesa otherwise turns on
# when the HOST has them (it found this machine's lm_sensors and failed on a
# header the guest does not have); off, so every host builds the same archives.
# A release build (Mesa's default is debugoptimized: assertions and debug
# checks in softpipe's inner loops, and debug info): the same picture, one
# binary for every machine either way - faster, and a smaller core.
# -Dshared-glapi=disabled is ESSENTIAL: otherwise _glapi_tls_Context lands only
# in a .so this core cannot link. zlib/expat are vendored via wraps.
opts="-Dforce_fallback_for=zlib,expat -Dgallium-drivers=swrast -Dvulkan-drivers= \
  -Dllvm=disabled -Dosmesa=true -Dopengl=true -Dglx=disabled -Degl=disabled \
  -Dgbm=disabled -Dglvnd=false -Dplatforms= -Dgles1=disabled -Dgles2=disabled \
  -Ddefault_library=static -Dbuild-tests=false -Dzstd=disabled -Dshared-glapi=disabled \
  -Dlmsensors=disabled -Dlibunwind=disabled -Dvalgrind=disabled -Dbuildtype=release -Db_ndebug=true"

# A cross build must not see the HOST's libraries. Without this, mesa's
# pkg-config finds the host libdrm and turns HAVE_LIBDRM on, and
# util/u_screen.c then includes xf86drm.h, which no guest sysroot has.
# zlib and expat are already forced to their wrap subprojects, so nothing
# legitimate needs pkg-config here.
mkdir -p "$mesa/no-pkgconfig"
PKG_CONFIG_LIBDIR="$mesa/no-pkgconfig"
PKG_CONFIG_PATH=""
export PKG_CONFIG_LIBDIR PKG_CONFIG_PATH

if [ -f "$build/build.ninja" ]; then
	"$meson" setup --reconfigure "$build" "$mesa" --cross-file "$mesa/guest-cross.ini" $opts
else
	"$meson" setup "$build" "$mesa" --cross-file "$mesa/guest-cross.ini" $opts
fi

[ "${MESA_BUILD_CONFIGURE_ONLY:-}" = 1 ] && { echo "mesa: configured OK (configure-only)"; exit 0; }

# Mesa's final shared osmesa .so fails to link for the guest (__dso_handle,
# -fno-pic, large model) - EXPECTED. This core links the static archives plus
# the target's own object, both built before that step. So tolerate the .so
# failure and verify the artifacts that are actually needed.
"$meson" compile -C "$build" -j "$jobs" || true

target_o="$(find "$build/src/gallium/targets/osmesa" -name 'target.c.o' 2>/dev/null | head -1)"
archives="$(find "$build" -name '*.a' 2>/dev/null | wc -l)"
if [ -n "$target_o" ] && [ "$archives" -gt 0 ]; then
	echo "mesa: ready - $archives archives + $target_o"
else
	echo "mesa: the build did NOT produce the osmesa target.c.o / archives" >&2
	exit 1
fi
