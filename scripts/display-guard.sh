#!/bin/bash
# display-guard.sh — NO TOOL BOOTS ON A DISPLAY THAT IS NOT A RIG (URGENT #195, 2026-11-21; CODING_RULES §7: never :0).
# WHY. An agent's shell inherits DISPLAY=:0 - the OWNER's display. warm-app-cache.sh took the tree's rig only when DISPLAY was
# EMPTY, so the very line fresh_home.sh's refusal printed, pasted into such a shell, booted jahlab on :0 ("booting ... jahlab on
# :0", spikes/one-frame-1/fork-revert-proof/warm-engine-off-arm.out; the dry reproduction spikes/te-tools/repro-195/). A rig
# display is :NN with NN in 60-99 (scripts/rig.sh's range); every other value - :0, :1, :10.0 of an ssh, a name - is not one.
#
# SOURCED by a tool that boots the app, jahlab or a fixture (the ONE definition; tests/hygiene/display_guard.sh holds it):
#   . "<tree>/scripts/display-guard.sh"
#   jah_display_guard <tool> <take-rig 0|1> -- <the tool's own argv, $0 first>
#       DISPLAY a rig display (:60-:99, a screen suffix allowed) -> returns 0, DISPLAY kept.
#       DISPLAY empty                                            -> returns 1: the caller takes the tree's rig (or, a row
#                                                                   wrapper, runs with none - its row decides).
#       DISPLAY anything else, take-rig 1 (the tool's --rig)     -> DISPLAY unset, returns 1: the caller takes the rig.
#       DISPLAY anything else, take-rig 0                        -> REFUSED: one line on stderr naming the value and the SAFE
#                                                                   line (env -u DISPLAY <the same command, absolute path>),
#                                                                   then `exit 2` - of the tool itself.
#   jah_rig_display <value>   exit 0 when <value> is a rig display, else 1 (no output).
#   jah_safe_line <argv...>   the argv as ONE line safe to paste into any shell: `env -u DISPLAY <abs argv0> <args, quoted>`.
# AS A COMMAND (python tools and the hygiene row): display-guard.sh check [<value>]  -> exit 0 a rig display, 1 empty, 2 not.
# (POSIX sh: fresh_home.sh runs under sh and sources this file - the functions a sh caller needs are POSIX; the bash-only
# printf %q is used by jah_safe_line, which only bash callers call)
jah_rig_display() {
    case "${1-}" in
        :6[0-9]|:[7-9][0-9]) return 0 ;;
        :6[0-9].*|:[7-9][0-9].*) case "${1#*.}" in ''|*[!0-9]*) return 1 ;; esac; return 0 ;;
    esac
    return 1
}
jah_safe_line() {
    local a0=$1 out; shift
    case "$a0" in /*) ;; */*) a0="$(cd "$(dirname "$a0")" 2> /dev/null && pwd)/$(basename "$a0")" ;; esac
    out="env -u DISPLAY $(printf '%q' "$a0")"
    [ $# -gt 0 ] && out="$out $(printf '%q ' "$@")"
    printf '%s\n' "${out% }"
}
jah_display_guard() {
    local tool=${1:?tool} rig=${2:?take-rig 0|1}; shift 2; [ "${1-}" = -- ] && shift
    local d=${DISPLAY-}
    jah_rig_display "$d" && return 0
    [ -z "$d" ] && return 1
    if [ "$rig" = 1 ]; then
        echo "$tool: DISPLAY=$d is not a rig display (60-99) - --rig: the tree's rig is used instead" >&2
        unset DISPLAY; return 1
    fi
    echo "$tool: REFUSED (exit 2): DISPLAY=$d is not a rig display (60-99; :0 is the OWNER's) - nothing booted. Run it with no DISPLAY (the tree's rig is taken): $(jah_safe_line "$@")" >&2
    exit 2
}
if [ -n "${BASH_VERSION-}" ] && eval '[ "${BASH_SOURCE[0]}" = "$0" ]'; then   # run, not sourced (a sh source skips it)
    case "${1-}" in
        check) v=${2-${DISPLAY-}}; jah_rig_display "$v" && exit 0; [ -z "$v" ] && exit 1; exit 2 ;;
        *) echo "usage: display-guard.sh check [<display>]   (sourced: jah_display_guard <tool> <0|1> -- <argv>)" >&2; exit 64 ;;
    esac
fi
