// RUN 5 of shadercache.app — THE SAVE UNDER CHURN (SMOKE-ENGINE-1 item 3).
//
// On 2026-09-14 the periodic shader-cache save killed the owner's editor and
// two rig instances inside forty minutes, always at the same place:
//
//   ShaderCache::save -> HlmsDiskCache::copyFrom -> Hlms::getProperty
//   -> IdString::operator<   SIGSEGV at 0x0
//
// copyFrom reads a renderable index and a pass index out of a 32-bit shader
// hash and subscripts two vectors with them, checking neither (ogre-patch 0035
// checks them now and logs instead of dying). Both indices grow with CHURN: the
// renderable cache with every new material/mesh permutation, the pass cache
// with every distinct set of pass properties — and a compositor rebuild, which
// is what every World post row is, produces new ones.
//
// So this run churns: every post row through every value, the player entered
// and left, and a SAVE after each step. It cannot prove the crash is gone,
// because the lane could not reproduce it on demand; what it does prove is that
// the save path survives the churn that grows those indices, and — through the
// driver, which reads the engine log — that the guard never had to fire.
function step(label) {
    if (!app.saveShaderCache())
        console.log("CHURN note: save declined at " + label + " (no new shaders is fine)");
}

var ROWS = [["ssr",         ["off", "half", "hq"]],
            ["ssao",        ["off", "half", "full"]],
            ["msaa",        ["off", "2x", "4x"]],
            ["smaa",        ["off", "medium", "ultra"]],
            ["refractions", ["off", "on"]],
            ["bloom",       ["off", "on"]],
            ["hdr",         ["off", "on"]]];

var steps = 0;
for (var round = 0; round < 3; ++round) {
    for (var r = 0; r < ROWS.length; ++r) {
        var id = ROWS[r][0], vals = ROWS[r][1];
        for (var v = 0; v < vals.length; ++v) {
            world.override({ id: id, value: vals[v] });
            editor.frame(3, 1.0 / 60.0);
            ++steps;
            step(id + "=" + vals[v]);
        }
    }
    player.play();
    editor.frame(10, 1.0 / 60.0);
    player.stop();
    editor.frame(5, 1.0 / 60.0);
    step("player round " + round);
}
world.clearOverrides();
editor.frame(5, 1.0 / 60.0);
step("cleared");

// THE CHURN THAT ACTUALLY CROSSED THE LINE (lane shadercache-2, 2026-09-14).
//
// The rows above were the suspicion; the SKY is the measurement. Every sky
// change re-captures the environment into a cube render target, and
// HlmsPbs::preparePassHash hashes that target's NAME into the pass properties
// (`target_envprobe_map`, OgreHlmsPbs.cpp:1813) — so a capture into a
// FRESHLY NAMED cube mints a permanent entry in Hlms::mPassCache. Measured in a
// live editor with the pass cache instrumented: one sky change, one entry,
// every time. The owner's evening session reached 1847 of them, seven times
// what the EIGHT BITS of the shader hash can address; past 256 the pass index
// spills into the renderable field beside it and HlmsDiskCache::copyFrom
// subscripts mRenderableCache out of range — the crash ogre-patch 0035 turned
// into a skipped entry and a log line.
//
// 300 changes therefore crosses 256 on its own. A/B on this exact script:
// before the fix (the capture cube named with processUniqueName) the engine
// logs "has more than 256 distinct pass property combinations"; after it (a
// RECYCLED name — irisgl EnginePrivate.h recycledName) it does not, and the
// two assertions the driver makes about this run's output are what say so.
//
// THE MEASUREMENT IS A DELTA (lane SMALL-ITEMS D, ledger §417). The regression
// is "each sky capture mints a permanent pass-cache entry", which is a RATE,
// and the absolute count at the end of this script is that rate plus a
// BASELINE nobody controls: the World-row churn above, the player round trips,
// and the disk cache runs 1-4 left behind. An absolute fence therefore fails
// the day somebody legitimately adds a pass property — a change with no
// regression in it — while the thing it guards is the step across this loop.
// So the state is reported TWICE, either side of the sky loop, and the driver
// asserts the difference.
console.log("SHADERCACHE-PRESKY " + JSON.stringify(app.shaderCache()));
var t0 = Date.now();
for (var sky = 0; sky < 300; ++sky) {
    world.sky("color", { color: { r: (sky * 7) % 255, g: (sky * 13) % 255,
                                  b: (sky * 29) % 255 } });
    editor.frame(2, 1.0 / 60.0);
    if (sky % 100 === 99) step("sky " + sky);
}
console.log("CHURN sky captures 300 in " + (Date.now() - t0) + " ms");
// The second half of the delta is read BEFORE the save: a save compiles
// nothing, but reading it here keeps the two samples separated by the sky loop
// and nothing else.
console.log("SHADERCACHE-POSTSKY " + JSON.stringify(app.shaderCache()));
step("sky churn");

console.log("CHURN steps " + steps);
console.log("SHADERCACHE " + JSON.stringify(app.shaderCache()));
app.quit();
