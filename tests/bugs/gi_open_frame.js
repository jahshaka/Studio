// bugs.gi_open_frame (BUGS-6 stage 0) — A SCENE CREATE LANDS NO LIGHTING WORK ON A PRESENTED FRAME.
// The script half; tests/bugs/test_gi_open_frame.cpp reads its markers and the engine's own lines
// between them. One create of the default template on a warm shader cache, the background compiler
// on as in an interactive session, then N frames counted (never a clock):
//   (a) the lighting arm is BOUND when the create returns and giStatus().rebuilds does not move
//       across the N frames after it (the arm was built under the open, not on a presented frame);
//   (b) no "compiles a permutation in the frame" line in those frames (the driver counts them);
//   (c) no Photon/Scroll* clone is made across the create (the driver reads the clone ids).
// THE DERIVATION OF N: the runner's create (spikes/bugs-6/create-only.out:127, the capture's frame 2591)
// put the WHOLE arm rebuild (297.9 ms) on the FIRST driver frame after the open, with 14 scroll clones;
// the longest wait the arm may still take after its inputs are asked for is the voxel-texture wait,
// kGiVoxelTextureWaitFrames = 30 (PhotonComponent.h), and a settle re-solve fires after kStableFrames = 15
// still frames. 60 frames covers both with the first frame inside: a deferred arm cannot hide past them.
var N = 60;
var r = { frames: [] };
app.setAsyncShaders(true);
function gi() {
    var s = world.giStatus();
    return { rebuilds: s.rebuilds, vctBound: s.vctBound, mode: s.mode };
}
r.before = gi();
console.log("BUGS6_CREATE_BEGIN");
project.create("bugs6 open frame");
console.log("BUGS6_WATCH");
r.atReveal = gi();
var worst = 0, worstAt = -1;
for (var i = 0; i < N; ++i) {
    var t0 = Date.now();
    editor.frame(1);
    var ms = Date.now() - t0;      // printed only: no millisecond bar in a guard
    if (ms > worst) { worst = ms; worstAt = i; }
    var s = gi();
    r.frames.push(s.rebuilds);
}
console.log("BUGS6_WATCHEND");
r.after = gi();
r.worstFrameMs = worst;
r.worstFrameAt = worstAt;
r.frames = r.frames.join(",");
console.log("BUGS6 " + JSON.stringify(r));
app.quit();
