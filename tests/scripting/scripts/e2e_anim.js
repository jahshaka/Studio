// scripting.e2e.anim — the anim.* verbs end to end (DEEP_AUDIT_2026_09 List B #7:
// the keyframe/timeline domain had no verbs at all, so none of it was testable).
//
// Runs headless (--headless: every anim verb is a document verb). Drives the
// whole keyframe domain through the API exactly as the Timeline panel does:
//   the animation list on a node, and which one is active
//   the animatable property surface (the panel's insert-key menu)
//   keyframe writes, overwrite-at-the-same-time, and read-back per channel
//   the document EVALUATION: seek poses the node between the keys
//   looping (time modulo length) and its off state (hold the last key)
//   key removal, track removal, animation removal
//   float and colour tracks (authored + sampled; see the note at that step)
//   the save/close/open round trip
//   the precondition errors (no active animation, unknown property, bad name)

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

function near(a, b, eps) {
    return Math.abs(a - b) <= (eps === undefined ? 0.001 : eps);
}

function throws(fn, what) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, what);
}

var proj = project.create("Anim Verbs " + Date.now());
assert(proj.length > 10, "project.create -> " + proj);

// The Timeline panel auto-creates an empty animation on whatever node is
// SELECTED (AnimationWidget::setSceneNode) — including in a headless run,
// where the panel is constructed but never shown, and scene.addPrimitive
// selects what it adds. So a script that wants a known animation surface
// starts by clearing it; that this works at all is the first thing tested.
function clearAnims(id) {
    var guard = 0;
    while (anim.list(id).length > 0) {
        anim.remove(id, "0");
        if (++guard > 32) throw new Error("clearAnims did not converge on " + id);
    }
}

function named(id, name) {
    var hits = anim.list(id).filter(function (a) { return a.name === name; });
    return hits.length === 1 ? hits[0] : null;
}

var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
assert(cube.length > 10, "scene.addPrimitive -> " + cube);

// ---- a node with no animations ----------------------------------------------
clearAnims(cube);
assert(anim.list(cube).length === 0, "anim.remove cleared the node's animations");

// The insert-key menu: only the property types a track exists for. The node
// reflects name/visible/castShadow/pickable too, and none of those can be keyed.
var props = anim.properties(cube);
var propNames = props.map(function (p) { return p.name; });
assert(propNames.indexOf("position") >= 0, "position is animatable");
assert(propNames.indexOf("rotation") >= 0, "rotation is animatable");
assert(propNames.indexOf("scale") >= 0, "scale is animatable");
assert(propNames.indexOf("visible") < 0, "a bool property is NOT offered: " + propNames.join(","));
assert(propNames.indexOf("name") < 0, "a string property is NOT offered");
for (var i = 0; i < props.length; ++i) {
    assert(props[i].animated === false, props[i].name + " has no track yet");
    assert(props[i].type === "vector3" || props[i].type === "float" || props[i].type === "color",
           props[i].name + " reports a track type: " + props[i].type);
}

// Writing a key before there is anything to write it on is an error with the
// fix in the message, not a crash and not a silent no-op.
throws(function () { anim.keyframe(cube, "position", 0); },
       "anim.keyframe with no active animation throws");
throws(function () { anim.length(cube); }, "anim.length with no active animation throws");

// ---- create / active --------------------------------------------------------
var a1 = anim.create(cube);
assert(a1 === "Animation1", "anim.create names it Animation1: " + a1);
var list = anim.list(cube);
assert(list.length === 1, "one animation on the node");
assert(list[0].active === true, "anim.create makes the new animation active");
assert(list[0].index === 0, "it is index 0");
assert(list[0].skeletal === false, "an authored animation carries no skeletal clip");
assert(list[0].properties.length === 0, "it starts with no tracks");
// Animation's constructor seeds length at 1 second; the first keyframe write
// recomputes it from the keys and it is derived from then on.
assert(anim.length(cube) === 1, "a fresh animation reports the constructor's 1 second: " +
       anim.length(cube));

// ---- keyframes --------------------------------------------------------------
assert(anim.keyframe(cube, "position", 0, { x: 0, y: 0, z: 0 }) === true,
       "anim.keyframe position @0");
assert(anim.keyframe(cube, "position", 2, { x: 10, y: 0, z: 0 }) === true,
       "anim.keyframe position @2");
assert(near(anim.length(cube), 2), "the length follows the last key: " + anim.length(cube));
assert(anim.properties(cube).filter(function (p) { return p.name === "position"; })[0].animated,
       "position now reports animated");
assert(anim.list(cube)[0].properties.indexOf("position") >= 0,
       "the animation lists its position track");

// Read-back is CHANNEL by channel — that is how the document stores a vec3.
var kf = anim.keyframes(cube, "position");
assert(kf.type === "vector3", "a position track is a vector3: " + kf.type);
assert(kf.tracks.length === 3, "three channels");
assert(kf.tracks[0].name === "X" && kf.tracks[1].name === "Y" && kf.tracks[2].name === "Z",
       "named X/Y/Z");
assert(kf.tracks[0].keys.length === 2, "X has two keys");
assert(near(kf.tracks[0].keys[0].time, 0) && near(kf.tracks[0].keys[0].value, 0), "X key 0 = (0, 0)");
assert(near(kf.tracks[0].keys[1].time, 2) && near(kf.tracks[0].keys[1].value, 10), "X key 1 = (2, 10)");
assert(kf.tracks[1].keys.length === 2 && kf.tracks[2].keys.length === 2,
       "Y and Z were keyed too (a whole property value in, all channels out)");

// sample() reads the curve without touching the scene.
assert(near(anim.sample(cube, "position", 1).x, 5), "sample @1 interpolates halfway: " +
       JSON.stringify(anim.sample(cube, "position", 1)));
assert(near(anim.sample(cube, "position", 0).x, 0), "sample @0 is the first key");
assert(near(anim.sample(cube, "position", 2).x, 10), "sample @2 is the last key");
assert(node.property(cube, "position").x === 0, "sampling did NOT move the node");

// ---- the evaluation: seek poses the document -------------------------------
assert(near(anim.seek(1), 1), "anim.seek(1) -> " + anim.seek());
assert(near(node.property(cube, "position").x, 5),
       "seek(1) posed the node halfway: x=" + node.property(cube, "position").x);
// t=2 on a 2-second LOOPING clip is t=0 — fmod, not "the end".
anim.seek(2);
assert(near(node.property(cube, "position").x, 0),
       "a looping clip wraps its own end: x=" + node.property(cube, "position").x);
anim.seek(1.99);
assert(near(node.property(cube, "position").x, 9.95, 0.01),
       "just before the end it is at the last key: x=" + node.property(cube, "position").x);
anim.seek(0);
assert(near(node.property(cube, "position").x, 0), "seek(0) posed it back at the start");
assert(near(anim.seek(), 0), "anim.seek() with no argument reads the clock");
anim.seek(-5);
assert(near(anim.seek(), 0), "a negative time clamps to zero");

// ---- looping ---------------------------------------------------------------
// A new animation loops by default (Animation's constructor), so t=3 on a
// 2-second clip samples t=1.
assert(anim.list(cube)[0].looping === true, "animations loop by default");
anim.seek(3);
assert(near(node.property(cube, "position").x, 5), "looping wraps t=3 to t=1: x=" +
       node.property(cube, "position").x);
assert(anim.setLooping(cube, false) === true, "anim.setLooping(false)");
assert(anim.list(cube)[0].looping === false, "and it reads back off");
anim.seek(3);
assert(near(node.property(cube, "position").x, 10), "not looping, t=3 holds the last key");
anim.setLooping(cube, true);

// ---- overwrite, not double --------------------------------------------------
assert(anim.keyframe(cube, "position", 2, { x: 20, y: 0, z: 0 }) === true, "re-key @2");
kf = anim.keyframes(cube, "position");
assert(kf.tracks[0].keys.length === 2, "a second key at the same time OVERWRITES: " +
       kf.tracks[0].keys.length + " keys");
assert(near(kf.tracks[0].keys[1].value, 20), "and carries the new value");
assert(near(anim.sample(cube, "position", 2).x, 20), "the overwritten key is what samples");
anim.setLooping(cube, false);
anim.seek(2);
assert(near(node.property(cube, "position").x, 20), "and what poses at the end of the clip");
anim.setLooping(cube, true);

// ---- a single key is a ZERO-LENGTH animation --------------------------------
// One key is one press of the insert button, and a looping animation samples
// time modulo its length: fmod(t, 0) is NaN, and a NaN sample poses NaN into
// the document and then into the engine. The clip must hold its one key.
var solo = scene.addPrimitive("sphere", { position: { x: 0, y: 0, z: 0 } });
clearAnims(solo);
anim.create(solo, "OneKey");
anim.keyframe(solo, "position", 0, { x: 7, y: 0, z: 0 });
assert(anim.length(solo) === 0, "a single key at t=0 is a zero-length animation");
assert(anim.list(solo)[0].looping === true, "and it loops");
anim.seek(5);
assert(near(node.property(solo, "position").x, 7),
       "a zero-length looping clip holds its key rather than posing NaN: x=" +
       node.property(solo, "position").x);
node.remove(solo);

// ---- keying the node's CURRENT value (the Timeline's insert button) --------
node.transform(cube, { position: { x: 4, y: 8, z: 0 } });
assert(anim.keyframe(cube, "position", 1) === true, "anim.keyframe with no value");
assert(near(anim.sample(cube, "position", 1).y, 8), "it keyed what the node held: " +
       JSON.stringify(anim.sample(cube, "position", 1)));

// ---- removal ----------------------------------------------------------------
assert(anim.removeKeyframe(cube, "position", 1) === true, "anim.removeKeyframe @1");
assert(anim.keyframes(cube, "position").tracks[0].keys.length === 2, "the key is gone");
assert(anim.removeKeyframe(cube, "position", 1) === false, "removing it again reports false");
assert(anim.removeKeyframe(cube, "position", 2) === true, "anim.removeKeyframe @2");
assert(near(anim.length(cube), 0),
       "the length follows the keys back down: " + anim.length(cube));

assert(anim.removeProperty(cube, "position") === true, "anim.removeProperty");
assert(anim.keyframes(cube, "position").tracks.length === 0, "the track is gone");
assert(anim.removeProperty(cube, "position") === false, "removing it again reports false");
assert(anim.list(cube)[0].properties.length === 0, "the animation has no tracks left");

// ---- several animations on one node ----------------------------------------
var a2 = anim.create(cube, "Walk");
assert(a2 === "Walk", "anim.create with a name");
assert(anim.list(cube).length === 2, "two animations");
assert(anim.list(cube)[1].active === true, "the new one is active");
assert(anim.setActive(cube, "Animation1") === true, "anim.setActive by name");
assert(anim.list(cube)[0].active === true, "Animation1 is active again");
assert(anim.setActive(cube, "1") === true, "anim.setActive by index");
assert(anim.list(cube)[1].active === true, "Walk is active again");
throws(function () { anim.setActive(cube, "Nope"); }, "anim.setActive with an unknown name throws");

// Keys land on the ACTIVE animation only.
anim.keyframe(cube, "scale", 0, { x: 1, y: 1, z: 1 });
anim.keyframe(cube, "scale", 1, { x: 3, y: 3, z: 3 });
assert(anim.list(cube)[1].properties.indexOf("scale") >= 0, "Walk carries the scale track");
assert(anim.list(cube)[0].properties.length === 0, "Animation1 was left alone");
anim.seek(0.5);
assert(near(node.property(cube, "scale").x, 2), "seek poses from the active animation: " +
       node.property(cube, "scale").x);

assert(anim.remove(cube, "Animation1") === true, "anim.remove by name");
assert(anim.list(cube).length === 1, "one animation left");
assert(anim.list(cube)[0].name === "Walk", "and it is the right one");

// ---- float and colour tracks ------------------------------------------------
var lamp = scene.addLight("point", { position: { x: 0, y: 3, z: 0 } });
assert(lamp.length > 10, "scene.addLight -> " + lamp);
clearAnims(lamp);
var lampProps = anim.properties(lamp).map(function (p) { return p.name; });
assert(lampProps.indexOf("intensity") >= 0, "a light offers intensity (float)");
assert(lampProps.indexOf("lightColor") >= 0, "a light offers lightColor (colour)");

anim.create(lamp, "Flicker");
anim.keyframe(lamp, "intensity", 0, 0);
anim.keyframe(lamp, "intensity", 1, 10);
var ikf = anim.keyframes(lamp, "intensity");
assert(ikf.type === "float" && ikf.tracks.length === 1, "an intensity track is one float channel");
assert(near(anim.sample(lamp, "intensity", 0.5), 5), "float sample interpolates: " +
       anim.sample(lamp, "intensity", 0.5));

anim.keyframe(lamp, "lightColor", 0, "#000000");
anim.keyframe(lamp, "lightColor", 1, "#ffffff");
var ckf = anim.keyframes(lamp, "lightColor");
assert(ckf.type === "color" && ckf.tracks.length === 4, "a colour track is R/G/B/A");
assert(ckf.tracks[0].name === "R" && ckf.tracks[3].name === "A", "named R..A");
var mid = anim.sample(lamp, "lightColor", 0.5);
assert(mid === "#7f7f7f" || mid === "#808080", "colour sample interpolates to grey: " + mid);

// Float and colour tracks POSE on a light (LightNode::updateAnimation), which
// is the other half of the evaluator: position/rotation/scale on any node,
// seven rows on a light, five on a decal, and nothing else anywhere.
anim.seek(0.5);
assert(near(node.property(lamp, "intensity"), 5),
       "seek posed the light's intensity: " + node.property(lamp, "intensity"));
anim.seek(1);
assert(near(node.property(lamp, "intensity"), 10), "and again at the last key");
var posed = node.property(lamp, "lightColor");
assert(posed === "#ffffff", "the colour track posed too: " + posed);

// ---- the round trip ---------------------------------------------------------
assert(project.save() === true, "project.save");
assert(project.open(proj) === true, "project.open (reopen)");

// Node guids survive the round trip, so the same ids still resolve.
var reopened = anim.list(cube);
assert(reopened.length === 1, "the cube kept its animation: " + reopened.length);
assert(reopened[0].name === "Walk", "with its name");
assert(reopened[0].active === true, "and it is still the active one");
assert(reopened[0].looping === true, "looping survived");
assert(reopened[0].properties.indexOf("scale") >= 0, "the scale track survived");
var skf = anim.keyframes(cube, "scale");
assert(skf.tracks.length === 3 && skf.tracks[0].keys.length === 2,
       "both keys survived on every channel");
assert(near(skf.tracks[0].keys[1].value, 3), "with their values");
assert(near(anim.length(cube), 1), "and the derived length: " + anim.length(cube));
anim.seek(0.5);
assert(near(node.property(cube, "scale").x, 2), "the reopened animation still poses");

var lampAnims = anim.list(lamp);
assert(lampAnims.length === 1 && lampAnims[0].name === "Flicker", "the light kept Flicker");
assert(lampAnims[0].properties.indexOf("intensity") >= 0 &&
       lampAnims[0].properties.indexOf("lightColor") >= 0,
       "with both of its tracks: " + lampAnims[0].properties.join(","));
assert(near(anim.sample(lamp, "intensity", 0.5), 5), "the float track reads the same after reopen");

// ---- errors -----------------------------------------------------------------
throws(function () { anim.keyframe(cube, "visible", 0, true); },
       "anim.keyframe on a non-animatable property throws");
throws(function () { anim.keyframe(cube, "nosuchprop", 0, 1); },
       "anim.keyframe on an unknown property throws");
throws(function () { anim.list("not-a-guid"); }, "anim.list on an unknown node throws");
assert(anim.keyframes(cube, "rotation").tracks.length === 0,
       "reading a property with no track is empty, not an error");
assert(anim.sample(cube, "rotation", 0) === undefined,
       "sampling a property with no track is undefined, not an error");

// Removing the active animation leaves the node with none — and the verbs that
// need one say so instead of dereferencing null.
assert(anim.remove(cube) === true, "anim.remove with no name removes the active one");
assert(anim.list(cube).length === 0, "no animations left");
throws(function () { anim.setLooping(cube, true); }, "anim.setLooping with none active throws");

console.log("scripting.e2e.anim PASSED");
