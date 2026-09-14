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
console.log("CHURN steps " + steps);
console.log("SHADERCACHE " + JSON.stringify(app.shaderCache()));
app.quit();
