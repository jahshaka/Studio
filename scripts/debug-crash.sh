#!/usr/bin/env bash
#
# debug-crash.sh — turn a Jahshaka crash into a readable backtrace.
#
# Two independent sources, both handled here because in practice you get one or
# the other and never know which in advance:
#
#   1. crash-<unixtime>.log — always produced, by src/app/crashhandler.cpp, in
#      the process's working directory (normally build-linux/bin). It is the
#      async-signal-safe backtrace_symbols_fd() dump: module + symbol + offset.
#      This script runs addr2line over it, which adds file:line and resolves the
#      frames -rdynamic cannot (statics, anonymous namespaces).
#
#   2. A core dump — only if the box is configured for it, which by DEFAULT it
#      is NOT (Ubuntu pipes core_pattern to apport and apport drops reports for
#      unpackaged binaries). See docs/BUILDING_LINUX.md, "Crash dumps on a dev
#      box", for the two settings. When a core exists this script runs gdb over
#      it, which is strictly better than (1): real frames, arguments, all
#      threads.
#
# Usage:
#   scripts/debug-crash.sh                       # newest of each, auto-found
#   scripts/debug-crash.sh --log crash-1234.log  # decode one crash log
#   scripts/debug-crash.sh --core ./core.12345   # gdb one core
#   scripts/debug-crash.sh --bin path/to/Jahshaka --log ...
#
# Nothing here modifies the system. It only reports what is configured.

set -u

SELF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SELF_DIR/.." && pwd)"

BIN=""
LOG=""
CORE=""

while [ $# -gt 0 ]; do
    case "$1" in
        --bin)  BIN="$2";  shift 2 ;;
        --log)  LOG="$2";  shift 2 ;;
        --core) CORE="$2"; shift 2 ;;
        -h|--help) sed -n '2,30p' "$0"; exit 0 ;;
        *) echo "unknown argument: $1 (try --help)" >&2; exit 2 ;;
    esac
done

# --- Locate the binary -------------------------------------------------------
if [ -z "$BIN" ]; then
    for candidate in \
        "$REPO_ROOT/build-linux/bin/Jahshaka" \
        "$REPO_ROOT/build/bin/Jahshaka" \
        "./Jahshaka"
    do
        [ -x "$candidate" ] && { BIN="$candidate"; break; }
    done
fi
if [ -z "$BIN" ] || [ ! -x "$BIN" ]; then
    echo "debug-crash: no Jahshaka binary found — pass --bin <path>" >&2
    exit 1
fi
BIN="$(cd "$(dirname "$BIN")" && pwd)/$(basename "$BIN")"
BIN_DIR="$(dirname "$BIN")"

echo "binary : $BIN"
if ! readelf --dyn-syms "$BIN" 2>/dev/null | grep -q 'MainWindow'; then
    echo "         NOTE: this binary has no exported C++ symbols — it was built"
    echo "               without -rdynamic, so crash-log frames will be raw"
    echo "               offsets. addr2line below still resolves them from DWARF."
fi

# --- Report (do not change) the box's core-dump configuration ----------------
report_core_config() {
    local pattern limit
    pattern="$(cat /proc/sys/kernel/core_pattern 2>/dev/null || echo '?')"
    limit="$(ulimit -c)"
    echo
    echo "core dumps: core_pattern = $pattern"
    echo "            ulimit -c    = $limit"
    case "$pattern" in
        \|*)
            echo "            -> core_pattern is a PIPE, so RLIMIT_CORE is IGNORED"
            echo "               (man 5 core). Raising 'ulimit -c' alone changes"
            echo "               NOTHING. See docs/BUILDING_LINUX.md."
            ;;
        *)
            if [ "$limit" = "0" ]; then
                echo "            -> ulimit -c is 0: no core will be written."
            fi
            ;;
    esac
}

# --- 1. Decode a crash-*.log -------------------------------------------------
find_newest_log() {
    ls -1t "$BIN_DIR"/crash-*.log ./crash-*.log 2>/dev/null | head -1
}

# Frame lines look like one of:
#   /path/Jahshaka(+0x1234ab) [0x55d3c0d0a1ab]        (no symbol: pre--rdynamic)
#   /path/Jahshaka(_ZN10MainWindow5closeEv+0x2a) [0x...]
#   /lib/x86_64-linux-gnu/libc.so.6(+0x2a1ca) [0x7f...]
#   /path/Jahshaka() [0x...]                          (nothing resolvable)
#
# For `+0xoff` the offset is already file-relative (PIE / shared object), which
# is exactly what addr2line wants. For `sym+delta` we look the symbol's file
# address up with nm and add delta.
symbol_file_offset() {           # $1 = module, $2 = symbol, $3 = delta (hex, 0x..)
    local base
    # `$3 == s` alone misses versioned symbols, which is how every glibc export
    # is spelled (`__libc_start_main@@GLIBC_2.34`); match the version suffix too.
    local match='$3==s || index($3, s "@")==1 {print $1; exit}'
    base="$(nm -D --defined-only "$1" 2>/dev/null | awk -v s="$2" "$match")"
    [ -z "$base" ] && base="$(nm --defined-only "$1" 2>/dev/null | awk -v s="$2" "$match")"
    [ -z "$base" ] && return 1
    printf '0x%x\n' $(( 0x$base + $3 ))
}

decode_log() {
    local log="$1"
    echo
    echo "=============================================================="
    echo "crash log: $log"
    echo "=============================================================="
    # Header (signal, fault address) verbatim — it is already readable.
    sed -n '1,6p' "$log"
    echo "--- decoded frames -------------------------------------------"
    local n=0
    while IFS= read -r line; do
        case "$line" in
            *"("*")"*"["*"]"*) ;;
            *) continue ;;
        esac
        local mod inner off sym delta pretty
        mod="${line%%(*}"
        inner="${line#*(}"; inner="${inner%%)*}"
        n=$((n + 1))
        printf '#%-3d %s\n' "$n" "$line"
        [ -f "$mod" ] || { echo "      (module not on disk: $mod)"; continue; }
        off=""
        case "$inner" in
            "")      ;;
            +0x*)    off="${inner#+}" ;;
            *+0x*)   sym="${inner%+0x*}"
                     delta="0x${inner##*+0x}"
                     off="$(symbol_file_offset "$mod" "$sym" "$delta")" || off=""
                     pretty="$(printf '%s' "$sym" | c++filt 2>/dev/null || printf '%s' "$sym")"
                     echo "      symbol: $pretty"
                     ;;
        esac
        if [ -n "$off" ]; then
            addr2line -C -f -p -i -e "$mod" "$off" 2>/dev/null | sed 's/^/      /'
        else
            echo "      (no decodable offset)"
        fi
    done < "$log"
    [ "$n" = 0 ] && echo "(no backtrace frames found in this file)"
}

# --- 2. gdb over a core ------------------------------------------------------
find_newest_core() {
    # Cores land wherever core_pattern says. The common dev-box settings are a
    # bare `core`/`core.%p` in the crashing process's cwd, or an absolute
    # directory. Look in the usual places; never guess at apport's spool.
    ls -1t "$BIN_DIR"/core "$BIN_DIR"/core.* ./core ./core.* \
           /var/lib/systemd/coredump/*Jahshaka* 2>/dev/null | head -1
}

gdb_core() {
    local core="$1"
    echo
    echo "=============================================================="
    echo "core: $core"
    echo "=============================================================="
    if ! command -v gdb >/dev/null 2>&1; then
        echo "gdb is not installed (sudo apt install gdb)"
        return
    fi
    gdb --batch --quiet \
        -ex "set pagination off" \
        -ex "thread apply all bt" \
        -ex "info registers rip" \
        "$BIN" "$core"
}

# --- Drive -------------------------------------------------------------------
report_core_config

if [ -n "$CORE" ]; then
    gdb_core "$CORE"
else
    auto_core="$(find_newest_core)"
    [ -n "$auto_core" ] && gdb_core "$auto_core"
fi

if [ -n "$LOG" ]; then
    [ -f "$LOG" ] || { echo "no such crash log: $LOG" >&2; exit 1; }
    decode_log "$LOG"
else
    auto_log="$(find_newest_log)"
    if [ -n "$auto_log" ]; then
        decode_log "$auto_log"
    else
        echo
        echo "no crash-*.log found in $BIN_DIR or the current directory."
    fi
fi
