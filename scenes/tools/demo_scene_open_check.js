// demo_scene_open_check.js — DEMO-SCENES-1: OPENS a demo project the make_demo_*.js scripts saved
// (@NAME@ = its project name — DEMO_NAME to demo_scene_run.sh —, @OUT@ = the evidence dir) the way the owner will — from the
// Desktop's row, a fresh process — renders it at rest and reports what the renderer achieved:
// giStatus at rest, the frame ms the monitor reads at the window's size, the engine's error
// log. Run it under VK_INSTANCE_LAYERS=VK_LAYER_KHRONOS_validation for the validation check.
var J = JSON.stringify;
var NAME = "@NAME@", OUT = "@OUT@";
app.engineErrors(true);
var t0 = Date.now();
if (project.open(NAME) !== true) throw new Error("open failed: " + NAME);
var openMs = Date.now() - t0;
var shot = editor.screenshot(OUT + "/" + NAME.replace(/ /g, "_").toLowerCase() + "_open.png", 1920, 1080, [], "scene");
app.frameStats({reset: true});
editor.frame(240);
var rs = app.renderStats(), gs = world.giStatus();
console.log("OPEN", J({name: NAME, openMs: openMs, frameMs: rs.frameMs, fps: rs.fps, sceneTriangles: rs.sceneTriangles,
    draws: rs.draws, giAtRest: gs.giAtRest, mode: gs.mode, cascades: (gs.cascades || []).length,
    cascadesPending: (gs.cascades || []).map(function (c) { return c.pending; }), cards: gs.cards,
    tier: world.photon().tier, worldMode: world.mode(), centre: shot.center}));
console.log("ENGINE_ERRORS", J(app.engineErrors()));
0;
