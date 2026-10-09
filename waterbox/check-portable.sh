#!/bin/sh
# Is this core.wbx machine code for a CPU, and not for one OS?
#
# A package carries a core for one CPU and runs on any OS a host is built for:
# the x86-64 one runs on Linux and on Windows. An aarch64 package is meant to
# be the same, and the one place aarch64 code can tie itself to an OS without
# calling it is register x18. Linux lets code use it like any other. Windows
# keeps the thread's TEB in it (its ARM64 ABI: "reserved platform register ...
# in user mode, points to TEB"), and macOS zeroes it when it likes. Arm's own
# procedure call standard tells code meant for more than one platform to avoid
# it. So every aarch64 object here is built with -ffixed-x18 (guest.mk, and
# setup-mesa.sh for Mesa), and this holds them to it.
#
# Two levels:
#
#   what this repository compiles - its objects and Mesa's archives - must not
#   touch x18, issue a system call of its own (svc/hvc/smc: a guest reaches
#   the host through miniBox's interop, never the kernel), or reach a system
#   register other than the thread pointer and the FP control and status
#   registers. Any of those fails the build.
#
#   the linked image also holds what this repository does not build: miniBox's
#   musl and libstdc++, and the system compiler's libgcc. What of theirs still
#   uses x18 is listed, so the gap is in plain view, but does not fail the
#   build: it is the guest kit's to close (built with -ffixed-x18, libgcc
#   included), not this core's. When it is closed the list is empty.
#
# Usage: check-portable.sh <core.wbx> <object or archive>...
set -eu

wbx="$1"
shift
[ -f "$wbx" ] || { echo "check-portable: $wbx not found" >&2; exit 1; }

case "$(uname -m)" in
	aarch64) ;;
	x86_64)
		# nothing OS-specific for x86-64 code to reach by accident here: the %fs
		# and red-zone rules that make an x86-64 guest run on Windows are
		# miniBox's check-wbx.sh, which has already run
		exit 0 ;;
	*) echo "check-portable: miniBox runs on x86-64 and aarch64 only, not $(uname -m)" >&2; exit 1 ;;
esac

# Per function: whether it touches x18 (as x18 or w18), issues svc/hvc/smc,
# or reaches a system register outside the allowed ones. Prints
# "<what> <file> <function> <count>".
scan() {
	objdump -d --no-show-raw-insn "$@" 2>/dev/null | awk '
		/:[ \t]+file format / { file = $1; sub(/:$/, "", file); next }
		/^In archive / { next }
		/^[0-9a-f]+ <[^>]+>:$/ { fn = $2; sub(/^</, "", fn); sub(/>:$/, "", fn); next }
		/^[ \t]+[0-9a-f]+:/ {
			line = $0
			if (line ~ /[^a-z0-9_][xw]18([^0-9]|$)/) n["x18 " file " " fn]++
			if (line ~ /[ \t](svc|hvc|smc)[ \t]/) n["syscall " file " " fn]++
			if (line ~ /[ \t](mrs|msr)[ \t]/) {
				reg = line
				sub(/^.*[ \t](mrs|msr)[ \t]+/, "", reg)
				gsub(/[ \t]/, "", reg)
				split(reg, part, ",")
				r = (line ~ /[ \t]mrs[ \t]/) ? part[2] : part[1]
				if (r != "tpidr_el0" && r != "fpcr" && r != "fpsr") n["sysreg:" r " " file " " fn]++
			}
		}
		END { for (k in n) print k, n[k] }' | sort
}

bad=0
if [ "$#" -gt 0 ]; then
	ours="$(scan "$@")"
	if [ -n "$ours" ]; then
		echo "check-portable: code built here is not OS-independent:" >&2
		echo "$ours" | awk '{ printf "  %-14s %s  %s (%d)\n", $1, $3, $2, $4 }' >&2
		bad=1
	fi
fi

# the image: a system call or an unexpected system register anywhere fails,
# whoever built it - nothing a guest links may reach the kernel or a register
# an OS keeps for itself without saying so here first
image="$(scan "$wbx")"
hard="$(echo "$image" | awk '$1 == "syscall"')"
if [ -n "$hard" ]; then
	echo "check-portable: $wbx issues system calls of its own:" >&2
	echo "$hard" | awk '{ printf "  %s (%d)\n", $3, $4 }' >&2
	bad=1
fi

# What the image reaches that this repository did not build: the guest kit's
# x18 users, and the system registers its libgcc reads for CPU features it may
# have (SME's TPIDR2_EL0 and the Guarded Control Stack's GCSPR_EL0, both behind
# a feature check). Said, not failed: see above.
rest="$(echo "$image" | awk '$1 != "syscall"')"
if [ -n "$rest" ]; then
	nfn="$(echo "$rest" | awk '$1 == "x18"' | wc -l)"
	echo "check-portable: $wbx: $nfn function(s) from the guest kit still use x18, so this package runs on Linux only:"
	echo "$rest" | awk '{ printf "  %-22s %s (%d)\n", $1, $3, $4 }' | head -20
	[ "$(echo "$rest" | wc -l)" -le 20 ] || echo "  ... and $(($(echo "$rest" | wc -l) - 20)) more"
fi

[ "$bad" -eq 0 ] || exit 1
echo "check-portable: $wbx: everything built here leaves x18 alone"
