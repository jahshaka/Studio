# owner_home_guard.sh — SOURCED (POSIX sh): jah_owner_root_refused <command...> — A TEST NEVER BOOTS THE APP ON THE LOGIN
# USER's OWN DATA ROOT (TEST-STREAMLINE-1 block A, 2026-11-03: a hand call of fresh_home.sh --warm <home> -- warm_app_boot.sh
# set no HOME, and the Debug app rewrote the owner's ~/.local/share/Jahshaka shader cache and opened his library).
# Returns 0 (and prints ONE line on stderr) when <command> IS the app (its program, after the LAUNCH WRAPPERS - env with its
# VAR= / -u VAR, nice, timeout with its DURATION, ionice, setsid, taskset with its mask, stdbuf, each with its options and
# their arguments, in any order and nesting (the landing audit's LOW, 2026-11-04: `timeout 60 <app>` was not judged) - is
# named Jahshaka; a wrapper SCRIPT that launches the app sets its own root and guards itself, as warm_app_boot.sh does) and the app's
# EFFECTIVE data root would be `getent passwd <login user>`'s home + /.local/share/Jahshaka: the app's own order
# (src/services/apppaths.h) - `--data-root <dir>`, else JAHSHAKA_DATA_ROOT, else ${XDG_DATA_HOME:-$HOME/.local/share}/Jahshaka -
# read from the environment and the command's own `env` words (VAR=..., -u VAR) before the app. Returns 1 otherwise: a
# command that is not the app (an engine fixture, a lab session: they name their cache directories themselves), a test
# home, a relative root (the app resolves it beside itself). The caller exits 2.
jah_owner_root_refused() {
    _jo=$(getent passwd "$(id -un)" 2>/dev/null | cut -d: -f6)
    [ -n "$_jo" ] || return 1
    _ja=0; _jd=""; _jp=""; _je=${JAHSHAKA_DATA_ROOT:-}; _jh=${HOME:-}; _jx=${XDG_DATA_HOME:-}
    _jwr=""; _jskip=0; _jarg=""   # the current wrapper, the positional words it takes before the program, an option's argument
    for _jw in "$@"; do
        if [ "$_ja" = 0 ]; then
            if [ -n "$_jarg" ]; then   # the argument of the wrapper option before it (env -u VAR unsets VAR)
                [ "$_jarg" = -u ] || [ "$_jarg" = --unset ] && case "$_jw" in
                    JAHSHAKA_DATA_ROOT) _je="" ;; XDG_DATA_HOME) _jx="" ;; HOME) _jh="" ;; esac
                _jarg=""; continue
            fi
            case "$_jw" in             # a launch wrapper: its options and positional words come next
                env|*/env) _jwr=env; _jskip=0; continue ;;
                nice|*/nice) _jwr=nice; _jskip=0; continue ;;
                timeout|*/timeout) _jwr=timeout; _jskip=1; continue ;;   # its DURATION
                ionice|*/ionice) _jwr=ionice; _jskip=0; continue ;;
                setsid|*/setsid) _jwr=setsid; _jskip=0; continue ;;
                taskset|*/taskset) _jwr=taskset; _jskip=1; continue ;;   # its mask or CPU list
                stdbuf|*/stdbuf) _jwr=stdbuf; _jskip=0; continue ;;
            esac
            case "$_jwr:$_jw" in       # a wrapper's option; the ones that take the next word as their argument
                env:-u|env:--unset|env:-C|env:--chdir|env:-S|env:--split-string|nice:-n|nice:--adjustment|\
                timeout:-s|timeout:--signal|timeout:-k|timeout:--kill-after|ionice:-c|ionice:--class|ionice:-n|\
                ionice:--classdata|stdbuf:-i|stdbuf:-o|stdbuf:-e) _jarg=$_jw; continue ;;
                ionice:-p|ionice:--pid|ionice:-P|ionice:--pgid|ionice:-u|ionice:--uid|taskset:-p|taskset:--pid) return 1 ;;   # not a launch
                ?*:-*) continue ;;
            esac
            if [ "$_jskip" -gt 0 ]; then _jskip=$((_jskip - 1)); continue; fi
            case "$_jw" in             # env's assignments; the FIRST other word is the program - judged only when it IS the app
                JAHSHAKA_DATA_ROOT=*) _je=${_jw#*=} ;;
                HOME=*) _jh=${_jw#*=} ;;
                XDG_DATA_HOME=*) _jx=${_jw#*=} ;;
                *=*) ;;
                Jahshaka|*/Jahshaka) _ja=1; _jp=""; continue ;;
                *) return 1 ;;
            esac
        else
            [ "$_jp" = --data-root ] && _jd=$_jw
            case "$_jw" in --data-root=*) _jd=${_jw#*=} ;; esac
        fi
        _jp=$_jw
    done
    [ "$_ja" = 1 ] || return 1
    if [ -n "$_jd" ]; then _jr=$_jd
    elif [ -n "$_je" ]; then _jr=$_je
    else _jr=${_jx:-$_jh/.local/share}/Jahshaka
    fi
    case "$_jr" in /*) ;; *) return 1 ;; esac
    [ "$(realpath -m "$_jr")" = "$(realpath -m "$_jo/.local/share/Jahshaka")" ] || return 1
    echo "REFUSED (exit 2): the app's data root would be the login user's own ($_jo/.local/share/Jahshaka) - a test boots" \
         "the app on a test home: HOME=<home>, --data-root or JAHSHAKA_DATA_ROOT under it (tests/support/owner_home_guard.sh)" >&2
    return 0
}
