#!/bin/sh
# Compare two golden outputs (run.sh's OUTDIRs): sh tools/golden/compare.sh BASE HEAD
# Exit 0 when nothing differs except krynn runs without --seed.
if [ $# -ne 2 ]; then
	echo "usage: sh tools/golden/compare.sh BASE HEAD" >&2
	exit 2
fi
here=$(cd "$(dirname "$0")" && pwd -P) || exit 2
base=$(cd "$1" && pwd -P) || exit 2
head=$(cd "$2" && pwd -P) || exit 2
exec python3 -I "$here/golden.py" compare "$base" "$head"
