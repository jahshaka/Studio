#!/usr/bin/env python3
"""lab.env_expectation_printed (LAB-1 stage 2): `jahlab --env-list` prints the whole catalogue — every environment the
spec names, its law (describe) and its closed-form expectation at each dolly stop d = 22 / 88 / 177 / 354 m, every
value a finite number. CPU only (the geometry and its feature table: no bake, no engine).
usage: env_expectation_printed.py <jahlab>"""
import math, re, subprocess, sys

ENVS = ["atom-street", "atom-arcade", "atom-forest", "atom-occlusion", "atom-small", "atom-flat", "photon-room",
        "photon-court"]
STOPS = ["22", "88", "177", "354"]
out = subprocess.run([sys.argv[1], "--env-list"], capture_output=True, text=True, timeout=50)
if out.returncode != 0:
    print("FAIL: --env-list exited %d\n%s" % (out.returncode, out.stderr[-2000:]))
    sys.exit(1)
bad = []
for e in ENVS:
    if not re.search(r"^%s \(seed \d+\): " % re.escape(e), out.stdout, re.M):
        bad.append("%s: not in the catalogue" % e)
        continue
    for d in STOPS:
        rows = re.findall(r"^  EXPECT %s d=%s m: (\S+) = (\S+)" % (re.escape(e), d), out.stdout, re.M)
        if not rows:
            bad.append("%s: no expectation at %s m" % (e, d))
        for q, v in rows:
            try:
                if not math.isfinite(float(v)):
                    bad.append("%s %s m %s: %s" % (e, d, q, v))
            except ValueError:
                bad.append("%s %s m %s: '%s' is not a number" % (e, d, q, v))
    print("%s: %d expectation rows" % (e, len(re.findall(r"^  EXPECT %s " % re.escape(e), out.stdout, re.M))))
if bad:
    print("FAIL:\n  " + "\n  ".join(bad))
    sys.exit(1)
print("lab.env_expectation_printed: PASS (%d environments x %d stops)" % (len(ENVS), len(STOPS)))
