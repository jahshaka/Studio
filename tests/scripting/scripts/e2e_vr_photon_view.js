// vr.photon_view — THE PHOTON VIEW WITH A HEADSET UP (PHOTON-VIEW-1 fix round, F6).
//
// A stereo chain's photon PASS_SCENE is instanced-stereo (applyStereo), and Ogre's voxel
// and probe visualizers are low-level materials whose vertex programs carry no per-eye
// matrix — the pin's stereo scheme does not serve them. So while a headset's view draws
// the scene, 'voxels' and 'probes' REFUSE through the ordinary refusal (giStatus().
// viewRefusals names "stereo"), every other mode stays paintable, a mode set before the
// session keeps going (the headset's own view never runs Ogre's visualizers; the desktop
// keeps painting them) and the refusal goes with the session.
// Frames, never time: the runtime's asks are counted (vr.state().rendered).
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }
function settle() {
    editor.frame(3);
    var st = world.giStatus();
    for (var i = 0; i < 200 && !st.giAtRest; ++i) { editor.frame(3); st = world.giStatus(); }
    return st;
}
var OTHERS = ["cards", "diffuse", "reflections"];

project.create("vr photon view " + Date.now());
world.gi({ tier: "high" });
var st = settle();
assert(st.viewRefusals.voxels === "" && st.viewRefusals.probes === "",
       "on the desktop alone 'voxels' and 'probes' can paint: " + J(st.viewRefusals));

// A mode set BEFORE the headset comes up.
assert(world.setPhotonView("voxels") === true, "'voxels' on before the session");
assert(vr.begin({ mirror: "left" }) === true, "the session began: " + app.lastError());
var before = vr.state().rendered;
for (var i = 0; i < 60 && vr.state().rendered < before + 20; ++i) editor.frame(1);
assert(vr.state().rendered >= before + 20,
       "the headset drew " + (vr.state().rendered - before) + " frames with the view on 'voxels'");
assert(world.photonView() === "voxels", "the mode set before the session stands (the desktop paints it)");

st = world.giStatus();
console.log("refusals with the headset up: " + J(st.viewRefusals));
assert(/stereo/.test(st.viewRefusals.voxels) && /stereo/.test(st.viewRefusals.probes),
       "with a stereo view live, 'voxels' and 'probes' are refused, naming stereo");
for (var k = 0; k < OTHERS.length; ++k)
    assert(st.viewRefusals[OTHERS[k]] === "", "'" + OTHERS[k] + "' is not refused by the headset");
var threw = "";
try { world.setPhotonView("probes"); } catch (e) { threw = String(e); }
assert(/stereo/.test(threw) && world.photonView() === "voxels",
       "world.setPhotonView('probes') refuses with the stereo reason and changes nothing");
assert(world.setPhotonView("diffuse") === true, "'diffuse' can be set with the headset up");
before = vr.state().rendered;
for (var j = 0; j < 60 && vr.state().rendered < before + 10; ++j) editor.frame(1);
assert(vr.state().rendered >= before + 10, "...and the headset keeps drawing");
assert(world.setPhotonView("off") === true, "off");

assert(vr.end() === true, "the session ended");
for (var f = 0; f < 60 && world.giStatus().viewRefusals.voxels !== ""; ++f) editor.frame(1);
st = world.giStatus();
assert(st.viewRefusals.voxels === "" && st.viewRefusals.probes === "",
       "the refusal goes with the session: " + J(st.viewRefusals));
"ok";
