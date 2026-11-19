#!/bin/bash
# Runs the simulator scripts in order against a fresh copy of the test card.
# Screenshots go to build/shots/. Exits non-zero if any check fails.
set -e
cd "$(dirname "$0")/../.."
make -s -C src/sim
out=build/shots card=build/simcard
rm -rf "$out" "$card" && mkdir -p "$out" "$card/apps"
cp apps.txt "$card/apps/"
head -c 4096 /dev/urandom > "$card/apps/typing.img"
head -c 4096 /dev/urandom > "$card/apps/graphcalc.img"
status=0
for t in tests/sim/[0-9]*.txt; do
	echo "== $t"
	./build/sim/sim "$t" "$out" "$card" >/dev/null || status=1
done
exit $status
