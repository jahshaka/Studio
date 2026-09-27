#!/bin/sh
# no_xid_run.sh <command> [args...] — runs a Vulkan app suite and then asserts that the
# kernel logged NO GPU fault (an NVIDIA 'NVRM: Xid' line) from ITS pid (VIEWS-DEPTH-1).
#
# A device loss is not always a red suite: the owner's session died of an Xid 13 while
# every suite that had driven the same verbs was green, and an Xid from a suite's own pid
# is never environmental (DOCS/traps/GATE_AND_RIG.md). So a suite that exercises a GPU
# hazard class wraps its run in this and fails on the kernel's word as well as its own.
#
# The kernel log is read through the journal (`journalctl -k`, readable by the adm
# group) and never with sudo — a test does not sudo. Only lines logged since the launch
# count, so an old fault of a reused pid cannot red a run. On Linux an unreadable
# journal is a RED, never a silent pass: the half of the assertion that matters would be
# missing. macOS has no NVRM log; the check is skipped there, and says so.
start=$(date +%s)
"$@" &
pid=$!
wait "$pid"
rc=$?
if [ "$(uname -s)" != "Linux" ]; then
    echo "no_xid_run: $(uname -s) has no NVRM kernel log; the Xid check is skipped"
    exit "$rc"
fi
# journald ingests the kernel ring asynchronously: give the fault's line a moment.
sleep 2
if ! log=$(journalctl -k --no-pager -q --since "@$start" 2>/dev/null); then
    echo "no_xid_run: the kernel journal is unreadable (journalctl -k) — the zero-Xid half cannot be asserted"
    exit 1
fi
hits=$(printf '%s\n' "$log" | grep "NVRM: Xid" | grep "pid=$pid,")
if [ -n "$hits" ]; then
    echo "no_xid_run: THE GPU FAULTED — Xid lines from pid $pid:"
    printf '%s\n' "$hits"
    exit 1
fi
echo "no_xid_run: zero Xid lines from pid $pid since the launch"
exit "$rc"
