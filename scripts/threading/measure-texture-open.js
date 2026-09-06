// measure-texture-open.js — one measured OPEN of the texture-heavy scene
// (SPECS/THREADING_ADOPTION_SPEC.md gate G2-c). Driven by texture-ab.sh, which
// decides which ARM this run is by setting (or not setting)
// JAH_TEXTURE_SYNC_LOAD / JAH_TEXTURE_MULTILOAD / JAH_TEXTURE_CACHE.
//
// WHAT IS MEASURED, and why not a stopwatch: the numbers come from the
// instruments the app already keeps, because a shell's wall clock around a
// process launch measures Qt starting up, the DB opening and the shader warm-up
// as well as the thing under test.
//
//   openMs        app.openTimings()[0].ms — LoadTimeline's own ledger for THIS
//                 open, from the open request to the scene being installed.
//   firstFrameMs  the script's clock from just before project.open to just
//                 after the first rendered frame. This is the user-visible
//                 "click to picture" number and the one the phase claims to
//                 improve; it INCLUDES openMs.
//   waitedMs      what the frame edge actually blocked on
//                 (app.waitForTextures) after that first frame — the residue
//                 the batching could not overlap. In the synchronous arm this
//                 is ~0 by construction, because the waiting already happened
//                 inside every loadTexture call, which is exactly the point.
//   slowFrames    app.frameStats().slowFrames — ticks over the 100 ms hitch
//                 threshold. The spec's prediction is that the number of long
//                 UI stalls falls MORE than the total does.
//   worstMs       the worst single tick.
//   loadRequests  how many textures the open actually asked for. A run where
//                 this is under 40 did not measure what it thinks it did.
//   errors        app.engineErrors().recorded — any of these invalidates a run.
//
// Prints ONE machine-readable RESULT line; the harness parses it.

function J(x) { return JSON.stringify(x); }

app.engineErrors(true);

var before = app.textureStreaming();
var t0 = Date.now();
var ok = project.open("TextureAB");
if (ok !== true) throw new Error("project.open(TextureAB) failed — run the fixture first");
// ONE frame, deliberately: the claim is about how long the FIRST picture takes,
// and every further frame would average the answer away.
editor.frame(1, 1.0 / 60.0);
var firstFrameMs = Date.now() - t0;

var waited = app.waitForTextures();
var after = app.textureStreaming();
var fs = app.frameStats();
var timings = app.openTimings();
var openMs = (timings && timings.length && timings[0].stage === "total") ? timings[0].ms : -1;
var errs = app.engineErrors();

console.log("timings: " + J(timings));
console.log("streaming before: " + J(before) + " after: " + J(after));
console.log("RESULT" +
            " openMs=" + openMs.toFixed(1) +
            " firstFrameMs=" + firstFrameMs +
            " waitedMs=" + waited.waitedMs.toFixed(1) +
            " slowFrames=" + fs.slowFrames +
            " worstMs=" + fs.worstMs.toFixed(1) +
            " loadRequests=" + (after.loadRequests - before.loadRequests) +
            " multiLoadThreads=" + after.multiLoadThreads +
            " metaEntries=" + after.metadataCacheEntries +
            " channelEntries=" + after.channelCacheEntries +
            " errors=" + errs.recorded);
