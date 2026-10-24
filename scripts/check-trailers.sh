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
# AN UNRESOLVABLE RANGE IS A REFUSAL, NEVER "clean" (URGENT #40, 2026-10-24): `git log` on an unknown revision printed
# its fatal line and an EMPTY list, and the loop below then reported "clean: 0 commits" and exit 0 - a trailer check
# that passes a range it never read. Both ends must name commits in <repo-dir>; exit 2 otherwise.
case "$rng" in
    *..*) ;;
    *) echo "check-trailers.sh: '$rng' is not a <base>..<tip> range" >&2; exit 2 ;;
esac
for end in "${rng%%..*}" "${rng##*..}"; do
    if ! git -C "$dir" rev-parse --verify --quiet "${end}^{commit}" >/dev/null; then
        echo "check-trailers.sh: '$end' is not a commit in $dir - the range $rng was NOT checked" >&2
        exit 2
    fi
done
if ! list=$(git -C "$dir" log --format=%h "$rng"); then
    echo "check-trailers.sh: git log $rng failed in $dir - the range was NOT checked" >&2
    exit 2
fi
hits=0
for c in $list; do
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
if [ "$hits" -eq 0 ]; then echo "clean: $(printf '%s\n' $list | grep -c .) commits in $rng, no trailers, all authored jahshaka"; exit 0; fi
echo "$hits violation(s) in $rng — send the lane back"; exit 1
