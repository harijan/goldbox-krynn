#!/bin/sh
# Golden transcripts: record every run of the port's binaries (build/eclplay
# and build/krynn) and of the tools the suites run (build/daximages,
# daxcompose, ecldump, daxcheck) in a built tree: args, status, the files
# each run wrote, stdout and stderr. compare.sh compares two trees' records;
# go.sh does both for a base revision and the working tree.
#
#   GOLDEN_DKK=1 sh tools/golden/run.sh TREE OUTDIR
#
# TREE must be built (make all build/krynn build/test_keymap). Its test
# suites run from TREE; GOLDEN_DKK=1 adds tests/test_death_knights.py. Their
# temp folders are under GOLDEN_TMP (default OUTDIR/tmp): go.sh gives both
# trees the same one, since a program may print its temp path (cut short),
# and the runs must agree. While this runs, those binaries are recording
# shims; the real ones come back when it ends, whatever happens.
set -u

if [ $# -ne 2 ]; then
	echo "usage: sh tools/golden/run.sh TREE OUTDIR" >&2
	exit 2
fi
here=$(cd "$(dirname "$0")" && pwd -P) || exit 2
tree=$(cd "$1" && pwd -P) || exit 2
mkdir -p "$2" || exit 2
out=$(cd "$2" && pwd -P) || exit 2
py="python3 -I $here/golden.py"

RECORDED="eclplay krynn daximages daxcompose ecldump daxcheck"
for kind in $RECORDED; do
	if [ ! -x "$tree/build/$kind" ]; then
		echo "run.sh: $tree/build/$kind is missing: build the tree first" >&2
		exit 2
	fi
	if [ -e "$tree/build/$kind.golden-real" ]; then
		echo "run.sh: $tree/build/$kind.golden-real exists: a run is already recording" >&2
		exit 2
	fi
done

tmp=${GOLDEN_TMP:-$out/tmp}
rm -rf "$out/golden.mk" "$out/results.txt" "$tmp"
for kind in $RECORDED; do
	rm -rf "$out/$kind"
	mkdir -p "$out/$kind"
done
mkdir -p "$tmp" || exit 2
TMPDIR=$tmp
export TMPDIR

restore() {
	for b in $RECORDED; do
		if [ -e "$tree/build/$b.golden-real" ]; then
			mv -f "$tree/build/$b.golden-real" "$tree/build/$b"
		fi
	done
}
trap restore EXIT
trap 'exit 130' INT TERM HUP

for b in $RECORDED; do
	mv "$tree/build/$b" "$tree/build/$b.golden-real" || exit 2
	cat > "$tree/build/$b" <<-EOF
	#!/bin/sh
	exec python3 -I '$here/golden.py' record $b '$tree/build/$b.golden-real' '$out' '$tree' '$tmp' -- "\$@"
	EOF
	chmod +x "$tree/build/$b"
done

results="$out/results.txt"
: > "$results"

suite() {
	name=$1
	log=$2
	shift 2
	(cd "$tree" && "$@") > "$out/$log" 2>&1
	rc=$?
	echo "$name $rc" >> "$results"
	echo "$name $rc"
}

suite test_eclplay.py test_eclplay.log python3 tests/test_eclplay.py

$py mk "$tree/Makefile" "$out/golden.mk" || exit 2
make -i -C "$tree" -f "$out/golden.mk" golden-sanitize > "$out/sanitize.log" 2>&1
rc=$?
errors=$(grep -c 'Error [0-9]' "$out/sanitize.log" || true)
echo "sanitize-eclplay $rc" >> "$results"
echo "sanitize-eclplay failures (make -i Error lines): $errors" >> "$results"
echo "sanitize-eclplay $rc (failures: $errors)"

suite test_images_cli.py test_images_cli.log python3 tests/test_images_cli.py
if [ "${GOLDEN_DKK:-}" = 1 ]; then
	suite test_death_knights.py test_death_knights.log python3 tests/test_death_knights.py
else
	echo "test_death_knights.py skipped (GOLDEN_DKK is not 1)" >> "$results"
fi
suite test_krynn.py test_krynn.log python3 tests/test_krynn.py

restore
trap - EXIT

echo "eclplay runs recorded: $(ls "$out/eclplay" | wc -l)" >> "$results"
echo "krynn runs recorded: $(ls "$out/krynn" | wc -l)" >> "$results"
tools=0
for kind in daximages daxcompose ecldump daxcheck; do
	tools=$((tools + $(ls "$out/$kind" | wc -l)))
done
echo "tool runs recorded: $tools" >> "$results"
rm -rf "$tmp"
tail -n 2 "$results"
exit 0
