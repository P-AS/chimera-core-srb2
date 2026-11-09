#!/bin/bash
# Publishes the gate's packages as this repository's rolling `dev` pre-release,
# replaced on every green push to main. Chimera's publish-core.sh publishes
# nightlies only since 2026-11-05 (ToolAssisted-run/chimera 09d87d78a); the dev
# build is this core's own, kept because its author still uses it. It is never
# Latest and never cited: a movie needs a nightly, which is never deleted.
#
# Each package is named as publish-core.sh names a nightly's,
# <core-id>-<version>-<arch>.chimeraCore, the version read out of the package.
#
# Usage: publish-dev.sh --core-id <id> --sha <commit> --tools <chimera>/tools
#                       --package <file> --arch <x86_64|aarch64> [--package ... --arch ...]
#                       [--asset <file>]... [--note <paragraph>]
set -eu

packages=()
arches=()
extra=()
core_id=""
sha=""
tools=""
note=""
while [ $# -gt 0 ]; do
	case "$1" in
		--package) packages+=("$2"); shift 2 ;;
		--arch) arches+=("$2"); shift 2 ;;
		--asset) extra+=("$2"); shift 2 ;;
		--core-id) core_id="$2"; shift 2 ;;
		--sha) sha="$2"; shift 2 ;;
		--tools) tools="$2"; shift 2 ;;
		--note) note="$2"; shift 2 ;;
		*) echo "unknown option: $1" >&2; exit 2 ;;
	esac
done
[ "${#packages[@]}" -gt 0 ] && [ "${#packages[@]}" -eq "${#arches[@]}" ] && [ -n "$core_id" ] && [ -n "$sha" ] && [ -n "$tools" ] || {
	echo "usage: publish-dev.sh --core-id <id> --sha <commit> --tools <dir> (--package <file> --arch <cpu>)..." >&2; exit 2; }

staging="$(mktemp -d)"
trap 'rm -rf "$staging"' EXIT

version=""
version_date=""
assets=()
for i in "${!packages[@]}"; do
	package="${packages[$i]}"
	arch="${arches[$i]}"
	case "$arch" in
		x86_64|aarch64) ;;
		*) echo "$package: arch must be x86_64 or aarch64, not $arch" >&2; exit 2 ;;
	esac
	read -r v vdate < <(python3 - "$package" <<'PY'
import json, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    config = json.loads(z.read("waterbox.config").decode("utf-8"))
print(config.get("version", "") or "-", config.get("versionDate", "").replace(" ", "T") or "-")
PY
)
	[ "$v" != - ] || { echo "$package stamps no version; the build must set CORE_VERSION" >&2; exit 1; }
	[ -z "$version" ] || [ "$v" = "$version" ] || {
		echo "the packages are of two versions, $version and $v; a release is one" >&2; exit 1; }
	version="$v"
	[ "$vdate" = - ] || version_date="$vdate"
	asset="$core_id-$version-$arch.chimeraCore"
	cp "$package" "$staging/$asset"
	assets+=("$asset")
done
made=""
[ -z "$version_date" ] || made="$(python3 "$tools/european-time.py" "$version_date")"
title="Development build ${version:0:8}${made:+ (made $made)}"

# what changed since the newest nightly, as a nightly's notes say it
repo_slug="${GITHUB_REPOSITORY:-$(gh repo view --json nameWithOwner --jq .nameWithOwner)}"
git fetch --tags --force --quiet 2>/dev/null || true
previous=$(git tag -l 'nightly-*' --sort=-refname | head -1)
changes="$(python3 "$tools/release-changes.py" --repo "$repo_slug" --since "$previous" --to "$sha" 2>/dev/null || true)"

notes="$staging/NOTES.md"
{
	echo "Core package for [Chimera](https://github.com/ToolAssisted-run/chimera)."
	echo
	echo "Download the \`.chimeraCore\` file below for your machine's CPU and put"
	echo "it in Chimera's \`Cores\` folder (**Tools > Core Manager** shows where"
	echo "that is). A package runs only on the CPU it is built for. Chimera"
	echo "downloads nothing itself."
	echo
	if [ "${#assets[@]}" -gt 1 ]; then
		echo "**On Windows or Linux you most likely want the x86_64 package.** Take the"
		echo "aarch64 one only if you know you are using an ARM device. On a Mac, take"
		echo "aarch64 for Apple silicon and x86_64 for an Intel Mac. Put only one of them"
		echo "in the folder."
		echo
	fi
	echo "| | |"
	echo "|---|---|"
	echo "| Core | \`$core_id\` |"
	echo "| Version | \`$version\` |"
	[ -z "$made" ] || echo "| Made | $made (Chimera lists it as \`$made  (${version:0:8})\`) |"
	echo "| Built from | \`$sha\` |"
	for a in "${assets[@]}"; do
		arch="${a%.chimeraCore}"; arch="${arch##*-}"
		echo "| $arch | \`$a\` |"
	done
	echo
	echo "This is a **development build**: it is replaced on every change, so it"
	echo "may stop being downloadable. For a build that is kept permanently -"
	echo "which is what a movie needs to stay replayable - take a nightly."
	if [ -n "$changes" ]; then
		echo
		printf '%s\n' "$changes"
	fi
	if [ -n "$note" ]; then
		echo
		printf '%s\n' "$note"
	fi
} > "$notes"

echo "publishing ${assets[*]} ${extra[*]+"${extra[*]}"} as dev from $sha"

# the API answers 5xx now and then, as publish-core.sh retries for
retry() {
	local n
	for n in 1 2 3 4; do
		"$@" && return 0
		[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: $1 $2 $3" >&2; sleep $((n * 15)); }
	done
	return 1
}

# The old dev goes first, tag and all, so the new one points at this commit.
# If it cannot be deleted, nothing is put into it: a dev holding packages of
# two commits is worse than an old one.
gone=0
for n in 1 2 3 4; do
	gh release delete dev --yes --cleanup-tag >/dev/null 2>&1 || true
	if look=$(gh release view dev 2>&1 >/dev/null); then
		:
	else
		case "$look" in *"not found"*) gone=1; break ;; esac
	fi
	[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: delete the old dev release" >&2; sleep $((n * 15)); }
done
[ "$gone" -eq 1 ] || { echo "the old dev release could not be deleted; nothing published" >&2; exit 1; }
# a create whose answer was lost may still have happened, so look first
created=0
for n in 1 2 3 4; do
	gh release view dev >/dev/null 2>&1 && { created=1; break; }
	gh release create dev --target "$sha" --prerelease --latest=false --title "$title" --notes-file "$notes" && { created=1; break; }
	[ "$n" -lt 4 ] && { echo "retrying in $((n * 15))s: release dev" >&2; sleep $((n * 15)); }
done
[ "$created" -eq 1 ] || exit 1
for a in "${assets[@]}"; do
	retry gh release upload dev "$staging/$a" --clobber
	echo "published $a"
done
for f in "${extra[@]+"${extra[@]}"}"; do
	retry gh release upload dev "$f" --clobber
	echo "published $(basename "$f")"
done
