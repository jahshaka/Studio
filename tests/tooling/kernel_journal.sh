#!/usr/bin/env bash
# devprocess.kernel_journal — THE KERNEL'S WORD IS READABLE (lane GATE-ADMIT-1; TESTING_GATE §4b).
# run_pool.py and tests/support/no_xid_run.sh read GPU faults (`NVRM: Xid … pid=N`) from the kernel
# journal to turn a device loss into a named CRASH; on a box where this user cannot read it, the
# pools print a FINDING and carry on, and THIS row is the one red that says so, with the fix.
# Linux only (macOS has no NVRM log: skipped, exit 77). Never sudo.
[ "$(uname -s)" = Linux ] || { echo "kernel_journal: $(uname -s) has no NVRM kernel log — skipped"; exit 77; }
if ! out=$(journalctl -k --no-pager -q -n 1 2>&1); then
    echo "kernel_journal: RED — \`journalctl -k\` is unreadable for user $(id -un): $out"
    echo "  the fix: sudo usermod -aG adm $(id -un)   (or systemd-journal), then log in again"
    exit 1
fi
case "$out" in
    *"insufficient permissions"*|*"No journal files"*)
        echo "kernel_journal: RED — journalctl -k opened no journal for $(id -un): $out"
        echo "  the fix: sudo usermod -aG adm $(id -un)   (or systemd-journal), then log in again"
        exit 1 ;;
esac
echo "kernel_journal: the kernel journal is readable by $(id -un) (groups: $(id -Gn))"
