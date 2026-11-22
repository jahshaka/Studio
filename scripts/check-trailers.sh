#!/usr/bin/env bash
# check-trailers.sh <base>..<tip> [repo-dir] [--upstream <sha>]
#
# THE TRAILER LAW, AS A COMMAND (2026-09-13). Every commit in a Jahshaka repo is
# authored `jahshaka` with NO attribution trailer — no `Claude-Session:`, no
# `Co-Authored-By:`, nothing (owner decree 2026-08-31; the harness reminder that
# asks for one is overridden). It was prose in the merge-audit law until three
# lane commits with `Claude-Session:` reached main in push #19 unnoticed. The lead
# runs this on every lane range BEFORE the merge commit exists; a hit sends the
# lane back. Exit 1 on any hit, 0 when clean, 2 when the range was not read.
#
# THE RULE GOVERNS THE COMMITS WE MAKE (CLAUDE.md "Commits", the owner 2026-11-21): UPSTREAM's own commits merged into
# our fork are accepted as they are. `--upstream <sha>` (the upstream tip the range merges, stated by the lead) SUBTRACTS
# exactly two classes and nothing else, each counted on the verdict line: (1) every commit of the range reachable from
# <sha>; (2) a commit of the range that is a cherry-pick of one of those - its `git patch-id --stable` equals that
# upstream commit's AND its author (name and email) is that commit's (a pick keeps upstream's author by design). A commit
# of ours with a trailer still faults; a foreign author on a patch that is not upstream's still faults; our merge
# commits are judged as ours. A <sha> whose newly merged history (<base>..<sha>) holds a commit authored or committed
# `jahshaka` is not upstream's: REFUSED, exit 2 (a mistaken sha - our own tip - can never whitewash our commits).
# Without the flag the check is exactly the law above.
set -u
usage="usage: check-trailers.sh <base>..<tip> [repo-dir] [--upstream <sha>]"
rng= dir= up=
while [ $# -gt 0 ]; do
    case "$1" in
        --upstream) [ $# -ge 2 ] && [ -n "$2" ] || { echo "check-trailers.sh: --upstream needs a sha - $usage" >&2; exit 2; }
                    up=$2; shift 2 ;;
        --upstream=*) up=${1#--upstream=}; [ -n "$up" ] || { echo "check-trailers.sh: --upstream needs a sha" >&2; exit 2; }; shift ;;
        -*) echo "check-trailers.sh: unknown option '$1' - $usage" >&2; exit 2 ;;
        *) if [ -z "$rng" ]; then rng=$1; elif [ -z "$dir" ]; then dir=$1
           else echo "check-trailers.sh: one range and one repo-dir - $usage" >&2; exit 2; fi; shift ;;
    esac
done
[ -n "$rng" ] || { echo "$usage" >&2; exit 2; }
dir="${dir:-.}"
# AN UNRESOLVABLE RANGE IS A REFUSAL, NEVER "clean" (URGENT #40, 2026-10-24): `git log` on an unknown revision printed
# its fatal line and an EMPTY list, and the loop below then reported "clean: 0 commits" and exit 0 - a trailer check
# that passes a range it never read. Both ends must name commits in <repo-dir>; exit 2 otherwise.
case "$rng" in
    *..*) ;;
    *) echo "check-trailers.sh: '$rng' is not a <base>..<tip> range" >&2; exit 2 ;;
esac
base="${rng%%..*}"
for end in "$base" "${rng##*..}" ${up:+"$up"}; do
    if ! git -C "$dir" rev-parse --verify --quiet "${end}^{commit}" >/dev/null; then
        echo "check-trailers.sh: '$end' is not a commit in $dir - the range $rng was NOT checked" >&2
        exit 2
    fi
done
if ! list=$(git -C "$dir" log --format=%H "$rng"); then
    echo "check-trailers.sh: git log $rng failed in $dir - the range was NOT checked" >&2
    exit 2
fi
# THE UPSTREAM SUBTRACTION (only with --upstream)
declare -A reach=() pick=()
note=
if [ -n "$up" ]; then
    if ! ups=$(git -C "$dir" log --format='%H %an|%cn' "$up" "^$base"); then
        echo "check-trailers.sh: git log $base..$up failed in $dir - the range was NOT checked" >&2
        exit 2
    fi
    ours=$(printf '%s\n' "$ups" | grep -E ' (jahshaka\|.*|.*\|jahshaka)$' | head -3)
    if [ -n "$ours" ]; then
        echo "check-trailers.sh: --upstream $up is not upstream's history: $base..$up holds commits authored or committed" \
             "jahshaka (${ours//$'\n'/; }) - the range $rng was NOT checked" >&2
        exit 2
    fi
    inrange=" $(printf '%s ' $list)"
    for u in $(printf '%s\n' "$ups" | cut -d' ' -f1); do
        case "$inrange" in *" $u "*) reach[$u]=1 ;; esac
    done
    # the patch-ids of upstream's newly merged commits (merges carry none), keyed to their authors
    declare -A upid=()
    while read -r pid u; do
        [ -n "$u" ] && upid[$pid]="${upid[$pid]:-} $u"
    done < <(git -C "$dir" log --no-merges --no-color --no-ext-diff -p --format='commit %H' "$up" "^$base" | git -C "$dir" patch-id --stable)
    # the candidates: commits of the range not reachable from upstream, not merges, authored by someone else
    cand=()
    for c in $list; do
        [ -n "${reach[$c]:-}" ] && continue
        [ "$(git -C "$dir" log -1 --format=%an "$c")" = jahshaka ] && continue
        [ -n "$(git -C "$dir" log -1 --format=%P "$c" | cut -s -d' ' -f2)" ] && continue
        cand+=("$c")
    done
    if [ ${#cand[@]} -gt 0 ]; then
        while read -r pid c; do
            [ -n "$c" ] && [ -n "${upid[$pid]:-}" ] || continue
            mine=$(git -C "$dir" log -1 --format='%an <%ae>' "$c")
            for u in ${upid[$pid]}; do
                if [ "$(git -C "$dir" log -1 --format='%an <%ae>' "$u")" = "$mine" ]; then
                    pick[$c]=$u
                    echo "PICK     ${c:0:9}  = upstream ${u:0:9} (patch-id ${pid:0:12}, author '$mine')  subtracted"
                    break
                fi
            done
        done < <(for c in "${cand[@]}"; do git -C "$dir" show --no-color --no-ext-diff --format='commit %H' "$c"; done | git -C "$dir" patch-id --stable)
    fi
    note=" (subtracted: ${#reach[@]} reachable from upstream ${up:0:9}, ${#pick[@]} pick(s) by patch-id)"
fi
hits=0 judged=0
for c in $list; do
    [ -n "${reach[$c]:-}" ] || [ -n "${pick[$c]:-}" ] && continue
    judged=$((judged+1))
    h=$(git -C "$dir" rev-parse --short "$c")
    if git -C "$dir" log -1 --format=%B "$c" | grep -qiE '^(claude-session|co-authored-by|signed-off-by: *claude)'; then
        echo "TRAILER  $h  $(git -C "$dir" log -1 --format=%s "$c" | cut -c1-70)"
        hits=$((hits+1))
    fi
    a=$(git -C "$dir" log -1 --format=%an "$c")
    if [ "$a" != "jahshaka" ]; then
        echo "AUTHOR   $h  '$a'  $(git -C "$dir" log -1 --format=%s "$c" | cut -c1-60)"
        hits=$((hits+1))
    fi
done
if [ "$hits" -eq 0 ]; then
    if [ -n "$up" ]; then
        echo "clean: $judged of $(printf '%s\n' $list | grep -c .) commits in $rng judged as ours$note, no trailers, all authored jahshaka"
    else
        echo "clean: $(printf '%s\n' $list | grep -c .) commits in $rng, no trailers, all authored jahshaka"
    fi
    exit 0
fi
echo "$hits violation(s) in $rng$note — send the lane back"; exit 1
