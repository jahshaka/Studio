# owner_home_guard.sh — SOURCED (POSIX sh): jah_owner_root_refused <command...> — A TEST NEVER BOOTS THE APP ON THE LOGIN
# USER's OWN DATA ROOT (TEST-STREAMLINE-1 block A, 2026-11-03: a hand call of fresh_home.sh --warm <home> -- warm_app_boot.sh
# set no HOME, and the Debug app rewrote the owner's ~/.local/share/Jahshaka shader cache and opened his library).
# Returns 0 (and prints ONE line on stderr) when <command> IS the app (its program, after env / VAR= / -u VAR / nice -n N, is
# named Jahshaka; a wrapper script that launches the app sets its own root and guards itself, as warm_app_boot.sh does) and the app's
# EFFECTIVE data root would be `getent passwd <login user>`'s home + /.local/share/Jahshaka: the app's own order
# (src/services/apppaths.h) - `--data-root <dir>`, else JAHSHAKA_DATA_ROOT, else ${XDG_DATA_HOME:-$HOME/.local/share}/Jahshaka -
# read from the environment and the command's own `env` words (VAR=..., -u VAR) before the app. Returns 1 otherwise: a
# command that is not the app (an engine fixture, a lab session: they name their cache directories themselves), a test
# home, a relative root (the app resolves it beside itself). The caller exits 2.
jah_owner_root_refused() {
    _jo=$(getent passwd "$(id -un)" 2>/dev/null | cut -d: -f6)
    [ -n "$_jo" ] || return 1
    _ja=0; _jd=""; _jp=""; _je=${JAHSHAKA_DATA_ROOT:-}; _jh=${HOME:-}; _jx=${XDG_DATA_HOME:-}
    for _jw in "$@"; do
        if [ "$_ja" = 0 ]; then
            # the words before the program: env and its VAR= / -u VAR, nice -n N; the FIRST other word is the program -
            # judged only when it IS the app (a wrapper script that launches it sets its own root, and guards itself)
            if [ "$_jp" = -u ]; then
                case "$_jw" in JAHSHAKA_DATA_ROOT) _je="" ;; XDG_DATA_HOME) _jx="" ;; HOME) _jh="" ;; esac
                _jp=$_jw; continue
            fi
            if [ "$_jp" = -n ]; then _jp=$_jw; continue; fi
            case "$_jw" in
                env|*/env|nice|*/nice|-u|-n) ;;
                JAHSHAKA_DATA_ROOT=*) _je=${_jw#*=} ;;
                HOME=*) _jh=${_jw#*=} ;;
                XDG_DATA_HOME=*) _jx=${_jw#*=} ;;
                *=*) ;;
                Jahshaka|*/Jahshaka) _ja=1 ;;
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
