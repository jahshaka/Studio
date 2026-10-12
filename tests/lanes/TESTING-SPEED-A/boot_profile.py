#!/usr/bin/env python3
"""testing_speed_a.boot_profile — WHERE A FIXTURE'S BOOT GOES (TESTING-SPEED-A item 10; SPECS/TESTING_SPEED_SPEC.md §2
A11: "the boot is the cost, not the check"). THIS LANE'S OWN ROW: deleted at the lane's land (tests/lanes/).

It runs the engine selftest (`Jahshaka --engine-selftest`, the six hash poses) TWICE on one private data root — COLD (an
empty root: no shader cache, no Hlms disk cache) then WARM (the same root) — and stamps every line of its output on
ARRIVAL (stdout/stderr line-buffered through stdbuf: Ogre's own lines carry no timestamp), then reads the boot's phases
between these markers:
    spawn -> "Vulkan: Initializing"                  the process: Qt, the app's services, the log, the plugins
          -> "Vulkan: Selected ... physical device"  the instance and the device detection (twice: the probe, the root)
          -> the first "Added resource location"     the device, the window, registerHlms
          -> the last "Added resource location"      THE MEDIA ARCHIVE SCAN
          -> the first "engine-selftest: pose 1"     the Hlms (disk cache load / compile), the scene, POSE 1's frames
          -> the last pose line                      the selftest's other poses
          -> exit                                    the teardown
and prints them per run (`BOOT <run> <phase> <seconds>`). PASS when both runs exit 0 and print every pose; the numbers
are the lane's measurement (spikes/testing-speed-a/boot.txt), never a bar.

Run: boot_profile.py <Jahshaka binary> <scratch dir>
"""
import os
import re
import shutil
import subprocess
import sys
import time

MARKS = [("process", re.compile(r"Vulkan: Initializing")),
         ("instance+detection", re.compile(r'Vulkan: Selected ".*" physical device')),
         ("device+window+hlms registration", re.compile(r"Added resource location")),
         ("media archive scan", None),            # the LAST resource-location line
         ("hlms+scene+pose 1", re.compile(r"engine-selftest: pose 1 ")),
         ("poses 2..6", None),                    # the LAST pose line
         ("teardown", None)]


def one(binary, root, png):
    t0 = time.monotonic()
    p = subprocess.Popen(["stdbuf", "-oL", "-eL", binary, "--data-root", root, "--engine-selftest", png],
                         stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace",
                         cwd=os.path.dirname(binary))
    lines = []
    for ln in p.stdout:
        lines.append((time.monotonic() - t0, ln.rstrip("\n")))
    rc = p.wait()
    end = time.monotonic() - t0
    first = lambda rx: next((t for t, l in lines if rx.search(l)), None)
    last = lambda rx: next((t for t, l in reversed(lines) if rx.search(l)), None)
    res_rx = re.compile(r"Added resource location")
    pose_rx = re.compile(r"engine-selftest: pose \S+ .*sha256")
    stamps = [0.0, first(MARKS[0][1]), first(MARKS[1][1]), first(res_rx), last(res_rx), first(MARKS[4][1]),
              last(pose_rx), end]
    poses = sum(1 for _, l in lines if pose_rx.search(l))
    return rc, stamps, poses


def main(binary, scratch):
    root = os.path.join(scratch, "boot-root")
    shutil.rmtree(root, ignore_errors=True)
    os.makedirs(root)
    ok = True
    for run in ("cold", "warm"):
        rc, st, poses = one(binary, root, os.path.join(scratch, f"boot-{run}.png"))
        print(f"BOOT {run}: exit {rc}, {poses} pose(s), {st[-1]:.2f} s in all")
        for k, (name, _) in enumerate(MARKS):
            a, b = st[k], st[k + 1]
            print(f"BOOT {run} {name:<34} " + (f"{b - a:7.2f} s" if a is not None and b is not None else "    n/a"))
        ok = ok and rc == 0 and poses >= 6
    shutil.rmtree(root, ignore_errors=True)
    print("testing_speed_a.boot_profile: " + ("PASS" if ok else "FAIL"))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.stdout.reconfigure(line_buffering=True)
    if len(sys.argv) != 3:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])))
