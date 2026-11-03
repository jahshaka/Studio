#!/bin/sh
# pool.clean_state.law — THE CLEAN-STATE LAW's OWN TEST (TEST-STREAMLINE-1 3a item 4 + its audit; tests/support/clean_state.py
# says the law). Toy arms on the NULL headless app:
#   0. clean_state.py's own self-test: the orders (forward, reversed, alone - FIRST in the pool process - for an arm first
#      in neither) and the normalisation (a home, a guid, a stamp and the NAMED `wall <n> ms` only; a bare "<n> ms" kept);
#   1. A LAW OVER NOTHING FAILS (audit finding 2): a dead "app" (exits at once, no arm runs) - exit 1, every arm NOT RUN;
#   2. leaves + reads + counts_in_ms + counts_law, no reset between arms: `reads` FAILs after `leaves` (caught by its
#      VERDICT), `counts_in_ms` passes but prints another engine-time "<n> ms" (caught: a law value is never normalised -
#      audit finding 3), `counts_law` prints another `LAW` value (caught: the law channel) - VIOLATED, each named;
#   3. leaves + counts_law WITH the clean-library baseline (app.resetLibrary between arms): the law HOLDS.
# Usage: clean_state_law.sh <python3> <clean_state.py> <run_pool.py> <Jahshaka> <dir of the toys and the baseline>
py=$1; law=$2; driver=$3; app=$4; d=$5
fail=0
say() { if [ "$1" = 0 ]; then echo "ok: $2"; else echo "FAIL: $2"; fail=1; fi; }
"$py" "$law" --self-test; say $? "0. the orders and the normalisation"

out=$("$py" "$law" --app "$d/dead_app.sh" --driver "$driver" --headless --pool law0 \
      --arm reads "$d/reads_the_library.js" 30 --arm leaves "$d/leaves_a_project.js" 30 2>&1); rc=$?
echo "$out" | sed 's/^/  | /'
[ $rc = 1 ] && echo "$out" | grep -q "^THE CLEAN-STATE LAW CANNOT BE JUDGED" && echo "$out" | grep -q "^NOT RUN reads in forward"
say $? "1. a law over NOTHING fails: a dead app, every arm NOT RUN, exit 1 (got $rc)"

out=$("$py" "$law" --app "$app" --driver "$driver" --headless --pool law2 --expect violated \
      --arm leaves "$d/leaves_a_project.js" 30 --arm reads "$d/reads_the_library.js" 30 \
      --arm counts_in_ms "$d/counts_in_ms.js" 30 --arm counts_law "$d/counts_law.js" 30 2>&1); rc=$?
echo "$out" | sed 's/^/  | /'
[ $rc = 0 ] && echo "$out" | grep -q "^VIOLATION reads: verdict" \
    && echo "$out" | grep -q "^VIOLATION counts_in_ms: law line 1 differs" \
    && echo "$out" | grep -q "^VIOLATION counts_law: law line 1 differs"
say $? "2. a left-behind row caught by the next arm's VERDICT, a moved engine-time ms and a moved LAW value by their LINES (no reset)"

"$py" "$law" --app "$app" --driver "$driver" --headless --pool law3 --baseline "$d/clean_library_baseline.js" \
    --arm leaves "$d/leaves_a_project.js" 30 --arm counts_law "$d/counts_law.js" 30
say $? "3. with the clean-library baseline between arms the law HOLDS (both orders)"

[ $fail = 0 ] && echo "pool.clean_state.law: all ok" || echo "pool.clean_state.law: FAILED"
exit $fail
