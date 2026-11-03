#!/bin/sh
# pool.clean_state.law — THE CLEAN-STATE LAW's OWN TEST (TEST-STREAMLINE-1 3a item 4; tests/support/clean_state.py says the
# law). Three toy arms on the NULL headless app, each pair in both orders:
#   1. leaves + reads, no reset between arms: `reads` FAILs after `leaves` and PASSes first - VIOLATED, caught (by verdict);
#   2. leaves + counts, no reset: `counts` PASSes in both orders but prints another number - VIOLATED, caught (by its line);
#   3. leaves + counts WITH the clean-library baseline (app.resetLibrary between arms): the law HOLDS;
#   0. first, clean_state.py's own self-test: the orders (forward, reversed, alone for an arm first in neither) and the
#      normalisation (only a home, a guid, a stamp and a wall are replaced - every other number is the law's).
# Usage: clean_state_law.sh <python3> <clean_state.py> <run_pool.py> <Jahshaka> <dir of the toys and the baseline>
py=$1; law=$2; driver=$3; app=$4; d=$5
fail=0
if "$py" "$law" --self-test; then echo "ok: 0. the orders and the normalisation"; else echo "FAIL: 0. clean_state.py --self-test"; fail=1; fi
step() { what=$1; shift; if "$py" "$law" --app "$app" --driver "$driver" --headless "$@"; then echo "ok: $what"; else echo "FAIL: $what"; fail=1; fi; }
step "1. a library row left behind is caught by the next arm's VERDICT (no reset)" --pool law1 --expect violated \
     --arm leaves "$d/leaves_a_project.js" 30 --arm reads "$d/reads_the_library.js" 30
step "2. ...and by its NUMBERS when the verdicts agree (no reset)" --pool law2 --expect violated \
     --arm leaves "$d/leaves_a_project.js" 30 --arm counts "$d/counts_the_library.js" 30
step "3. with the clean-library baseline between arms the law HOLDS (the same pair, both orders)" --pool law3 \
     --baseline "$d/clean_library_baseline.js" --arm leaves "$d/leaves_a_project.js" 30 --arm counts "$d/counts_the_library.js" 30
[ $fail = 0 ] && echo "pool.clean_state.law: all ok" || echo "pool.clean_state.law: FAILED"
exit $fail
