#!/bin/bash
# lab.envs_build (LAB-1 stage 2): `jahlab --env-check --env NAME` for every environment, the eight in parallel (each a
# process of its own: the row's wall is the slowest build, not their sum) — each builds and bakes under 5 s, rebuilds
# and re-bakes deterministically (the same triangles, scene hash and bake hash), and the next seed changes the hash.
# usage: envs_build.sh <jahlab>
set -u
LAB=${1:?jahlab}
envs=$("$LAB" --env-list | sed -n 's/^\([a-z][a-z-]*\) (seed [0-9]*): .*/\1/p')
[ "$(echo "$envs" | wc -w)" -ge 8 ] || { echo "FAIL: the catalogue lists $(echo "$envs" | wc -w) environments"; exit 1; }
tmp=$(mktemp -d)
for e in $envs; do "$LAB" --env-check --env "$e" > "$tmp/$e.log" 2>&1 & done
wait
fail=0
for e in $envs; do
    grep '^ENV-CHECK [a-z]' "$tmp/$e.log" | grep -v 'PASSED\|FAILED'
    grep -q '^ENV-CHECK PASSED' "$tmp/$e.log" || { echo "FAIL: $e"; tail -5 "$tmp/$e.log"; fail=1; }
done
rm -rf "$tmp"
[ $fail -eq 0 ] && echo "lab.envs_build: PASS" || echo "lab.envs_build: FAIL"
exit $fail
