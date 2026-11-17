#!/bin/sh
# Golden transcripts end to end: the port at BASE_REV (a detached worktree of
# this repository, made under OUT and removed after) and at HEAD_TREE, both
# built and recorded side by side, then compared. Usage:
#   sh tools/golden/go.sh BASE_REV HEAD_TREE OUT
# Prints the comparison; OUT/cmp.txt keeps it and OUT/out-base, OUT/out-head
# keep the records. Exit 0 when nothing differs (except unseeded krynn runs).
set -u

if [ $# -ne 3 ]; then
	echo "usage: sh tools/golden/go.sh BASE_REV HEAD_TREE OUT" >&2
	exit 2
fi
here=$(cd "$(dirname "$0")" && pwd -P) || exit 2
head=$(cd "$2" && pwd -P) || exit 2
mkdir -p "$3" || exit 2
out=$(cd "$3" && pwd -P) || exit 2
base="$out/base"
jobs=${GOLDEN_JOBS:-16}
start=$(date +%s)

cleanup() {
	git -C "$head" worktree remove --force "$base" > /dev/null 2>&1
	rm -rf "$base"
	git -C "$head" worktree prune
}
cleanup
git -C "$head" worktree add --detach "$base" "$1" > /dev/null || exit 2
trap cleanup EXIT
trap 'exit 130' INT TERM HUP

# The base worktree gets the head's asset folders and saves (top-level links).
for entry in "$head"/*; do
	[ -L "$entry" ] || continue
	name=${entry##*/}
	if [ ! -e "$base/$name" ] && [ ! -L "$base/$name" ]; then
		ln -s "$(readlink -f "$entry")" "$base/$name" || exit 2
	fi
done

make -C "$base" -j"$jobs" all build/krynn build/test_keymap > "$out/build-base.log" 2>&1 || {
	echo "base build failed: $out/build-base.log" >&2
	exit 2
}
make -C "$head" -j"$jobs" all build/krynn build/test_keymap > "$out/build-head.log" 2>&1 || {
	echo "head build failed: $out/build-head.log" >&2
	exit 2
}

# One after the other, with one temp folder path for both (see run.sh).
export GOLDEN_TMP="$out/tmp"
sh "$here/run.sh" "$base" "$out/out-base" > "$out/run-base.log" 2>&1 || {
	echo "base run failed: $out/run-base.log" >&2
	exit 2
}
sh "$here/run.sh" "$head" "$out/out-head" > "$out/run-head.log" 2>&1 || {
	echo "head run failed: $out/run-head.log" >&2
	exit 2
}

sh "$here/compare.sh" "$out/out-base" "$out/out-head" > "$out/cmp.txt"
status=$?
cat "$out/cmp.txt"
echo "wall time: $(( $(date +%s) - start ))s"
exit $status
