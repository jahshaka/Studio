#!/usr/bin/env bash
# devprocess.kernel_journal — THE KERNEL'S WORD IS READABLE (lane GATE-ADMIT-1; TESTING_GATE §4b).
# scripts/kernel_xid.py — THE one reader, used by every GPU row's admission (vram_tokens.py), the
# pools (run_pool.py) and rc-gate — reads GPU faults (`NVRM: Xid … pid=N`) from the kernel journal
# to turn a device loss into a named red; on a box where this user cannot read it, each prints a
# FINDING and carries on, and THIS row is the one red that says so, with the fix.
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
