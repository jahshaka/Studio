#!/usr/bin/env bash
# check-trailers.sh <base>..<tip> [repo-dir]
#
# THE TRAILER LAW, AS A COMMAND (2026-09-13). Every commit in a Jahshaka repo is
# authored `jahshaka` with NO attribution trailer — no `Claude-Session:`, no
# `Co-Authored-By:`, nothing (owner decree 2026-08-31; the harness reminder that
# asks for one is overridden). It was prose in the merge-audit law until three
# lane commits with `Claude-Session:` reached main in push #19 unnoticed. The lead
# runs this on every lane range BEFORE the merge commit exists; a hit sends the
# lane back. Exit 1 on any hit, 0 when clean.
set -u
rng="${1:?usage: check-trailers.sh <base>..<tip> [repo-dir]}"
dir="${2:-.}"
hits=0
for c in $(git -C "$dir" log --format=%h "$rng"); do
    if git -C "$dir" log -1 --format=%B "$c" | grep -qiE '^(claude-session|co-authored-by|signed-off-by: *claude)'; then
        echo "TRAILER  $c  $(git -C "$dir" log -1 --format=%s "$c" | cut -c1-70)"
        hits=$((hits+1))
    fi
    a=$(git -C "$dir" log -1 --format=%an "$c")
    if [ "$a" != "jahshaka" ]; then
        echo "AUTHOR   $c  '$a'  $(git -C "$dir" log -1 --format=%s "$c" | cut -c1-60)"
        hits=$((hits+1))
    fi
done
if [ "$hits" -eq 0 ]; then echo "clean: $(git -C "$dir" log --format=%h "$rng" | wc -l) commits in $rng, no trailers, all authored jahshaka"; exit 0; fi
echo "$hits violation(s) in $rng — send the lane back"; exit 1
