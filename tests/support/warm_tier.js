// warm_tier.js — ONE TEST TIER's BORN WORLD, DRAWN (HARNESS-1; scripts/warm-app-cache.sh's app-off boot runs it once per
// line of <build>/tests/test-boot-sets.txt, the process booted at that JAHSHAKA_TEST_TIER / JAHSHAKA_TEST_NEEDS). Not a
// test: the editor's viewport draws the World a row of that tier is born with, so its Hlms permutations and pipelines
// are compiled into the warm-up's cache (the row's own run then loads them: 0 bytes added). 60 frames = one second of
// the fixed clock: the blocking pass builds the scene's permutations in the first frames, the GI's compute set in its
// first dispatches (the startup gate's own quiet window is 30 frames).
editor.frame(60);
console.log("warm_tier: 60 frames drawn");
