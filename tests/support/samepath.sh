# samepath.sh — PATH COMPARISONS THAT SURVIVE A SYMLINKED ROOT (STAGE-CLOSE-TRIAGE-1, 2026-10-10).
# Sourced by the shell suites (`. "$(dirname "$0")/../support/samepath.sh"`), never run.
#
# WHY. The box's workspace root is a symlink (/home/jahshaka/Developer -> /mnt/work/Developer). A tree
# configured through the /home spelling hands its suites /home paths (HOME=, JAHSHAKA_DATA_ROOT=, argv),
# while the kernel answers with the PHYSICAL one: ctest chdir()s into WORKING_DIRECTORY and bash's $PWD
# is then getcwd() (/mnt/work/...), and the app's applicationDirPath() comes from /proc/self/exe. Five
# rows compared the two spellings as strings and redded 3/3 on a correct product (A2-defect-*).
# THE RULE: a path is compared with a path only after BOTH sides are resolved (realpath -m: symlinks
# followed, a not-yet-existing tail kept) — never a string match of one spelling against another.

# jah_phys <path> — the physical spelling (no symlinks, no . or ..); the path need not exist yet.
jah_phys() { realpath -m -- "$1"; }

# jah_same_path <a> <b> — the two name the same place.
jah_same_path() { [ "$(jah_phys "$1")" = "$(jah_phys "$2")" ]; }

# jah_inside <path> <dir> — <path> is <dir> or below it (both resolved).
jah_inside() {
    _jp="$(jah_phys "$1")"; _jd="$(jah_phys "$2")"
    case "$_jp" in "$_jd"|"$_jd"/*) return 0 ;; esac
    return 1
}

# jah_logged <log> <KEY> — the value of the first `KEY=<value>` line a script printed (a console.log
# may carry a prefix; KEY must stand alone, so ROOT= never matches DATAROOT=).
jah_logged() { sed -n -E "s/^(.*[^A-Za-z0-9_])?$2=//p" "$1" | head -1; }
