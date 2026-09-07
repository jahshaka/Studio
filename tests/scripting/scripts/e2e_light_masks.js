// scripting.e2e.light_masks — LIGHTING CHANNELS end to end in the real app:
// the verbs, the reflected property, the document defaults, and the save ->
// close -> open round trip.
//
// The pixel proof that the channels actually filter light lives in the
// lights.masks engine suite; this is the API-first half — that the capability
// is reachable, spelled consistently, refuses nonsense with a message, and
// SURVIVES A REOPEN (a mask that is silently dropped on load is worse than no
// mask, because the scene looked right when it was authored).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var guid = project.create("Light Masks " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
var lamp = scene.addLight("point", { position: { x: 0, y: 3, z: 0 } });
assert(cube.length > 10 && lamp.length > 10, "a cube and a light");
editor.frame(2);

// ---- defaults ------------------------------------------------------------
// Every node starts on every channel. That is what makes turning the feature on
// a no-op for every scene that existed before it.
var m = node.lightMask(cube);
console.log("cube lightMask = " + JSON.stringify(m));
assert(m.all === true, "a new node is on EVERY channel");
assert(m.mask === 4294967295, "...which reads as the unsigned 0xFFFFFFFF");
assert(m.channels === undefined,
       "...and the channel list is omitted rather than listing all 32");
assert(node.lightMask(lamp).all === true, "a new light is on every channel too");

// The reflected row is the SIGNED spelling of the same bits (Unity's culling
// mask does exactly this), so -1 is 'everything'.
assert(node.property(cube, "lightMask") === -1,
       "node.property reports the same bits signed: -1 = every channel");

// ---- the array form ------------------------------------------------------
assert(node.setLightMask(cube, [1]), "node.setLightMask(cube, [1])");
m = node.lightMask(cube);
console.log("cube after [1] = " + JSON.stringify(m));
assert(m.mask === 2, "channel 1 alone is the mask 2");
assert(m.all === false, "...and the node is no longer on every channel");
assert(m.channels.length === 1 && m.channels[0] === 1, "channels reads back [1]");
assert(node.property(cube, "lightMask") === 2, "the reflected row agrees");

assert(node.setLightMask(cube, [0, 3, 31]), "an array may name any channel 0..31");
m = node.lightMask(cube);
assert(m.mask === 1 + 8 + 2147483648, "0|3|31 -> " + m.mask);
assert(m.channels.length === 3 && m.channels[2] === 31, "channel 31 is reachable from a script");

assert(node.setLightMask(cube, []), "an EMPTY array is legal");
assert(node.lightMask(cube).mask === 0, "...and means 'no channels': lit by nothing");

// ---- the number form, both spellings of 'everything' ---------------------
assert(node.setLightMask(cube, 4294967295), "node.setLightMask(cube, 4294967295)");
assert(node.lightMask(cube).all === true, "the unsigned spelling of 'all' is accepted");
assert(node.setLightMask(cube, 6), "a raw mask goes in as a number");
assert(node.lightMask(cube).mask === 6, "6 = channels 1 and 2");
assert(node.setLightMask(cube, -1), "-1 is the signed spelling of the same thing");
assert(node.lightMask(cube).all === true, "...and means every channel");

// The reflected setter takes both too — this is the trap the wide read exists
// for: value.toInt() on 4294967295 yields 0, i.e. NO channels, which is the
// exact opposite of what the caller asked for.
assert(node.setProperty(cube, "lightMask", 4294967295),
       "node.setProperty(lightMask, 4294967295)");
assert(node.lightMask(cube).all === true, "...is 'everything', not 'nothing'");
assert(node.setProperty(cube, "lightMask", -1), "node.setProperty(lightMask, -1)");
assert(node.lightMask(cube).all === true, "...same 32 bits");

// ---- refusals ------------------------------------------------------------
var threw = "";
try { node.setLightMask(cube, [32]); } catch (e) { threw = String(e); }
assert(threw.indexOf("0..31") >= 0, "a channel index above 31 is refused: " + threw);
threw = "";
try { node.setLightMask(cube, ["red"]); } catch (e) { threw = String(e); }
assert(threw.indexOf("channel index") >= 0, "a non-numeric channel is refused: " + threw);
threw = "";
try { node.setLightMask("no-such-node", [1]); } catch (e) { threw = String(e); }
assert(threw.indexOf("no node with id") >= 0, "an unknown node is refused: " + threw);

// ---- the row is listed, so a model can find it without guessing ----------
var rows = node.properties(cube);
var row = null;
for (var i = 0; i < rows.length; ++i) if (rows[i].name === "lightMask") row = rows[i];
assert(row !== null, "node.properties lists the lightMask row");
assert(row.writable === true, "...and says it is writable");
assert(row.displayName === "Lighting Channels", "...under the name the panel uses");

// ---- both ends of the feature -------------------------------------------
assert(node.setLightMask(lamp, [1]), "a LIGHT takes a mask the same way");
assert(node.lightMask(lamp).mask === 2, "the light is on channel 1");
assert(node.setLightMask(cube, [2]), "and the cube is on channel 2");
editor.frame(3);
// Nothing here asserts pixels — that is lights.masks' job — but the frames must
// go through cleanly: the mirror pushes both halves every sync and a bad push
// would show up as an engine error.
assert(scene.nodes().length >= 2, "the scene still renders after both masks are set");

// ---- save -> close -> open ----------------------------------------------
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");
editor.frame(2);

var nodes = scene.nodes();
var reCube = null, reLamp = null;
for (var j = 0; j < nodes.length; ++j) {
    if (nodes[j].id === cube) reCube = nodes[j].id;
    if (nodes[j].id === lamp) reLamp = nodes[j].id;
}
assert(reCube !== null && reLamp !== null, "both nodes came back");
assert(node.lightMask(reCube).mask === 4, "the cube's channel survived the round trip");
assert(node.lightMask(reLamp).mask === 2, "the light's channel survived the round trip");

// And the DEFAULT is what an absent key means: the writer only emits the mask
// for a node that is not on every channel, so a node left alone must come back
// on all of them rather than on none.
var other = scene.addPrimitive("cube", { position: { x: 3, y: 0, z: 0 } });
assert(node.lightMask(other).all === true, "a fresh node is on every channel");
assert(project.save() === true, "project.save (with one unmasked node)");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");
editor.frame(2);
assert(node.lightMask(other).all === true,
       "a node with no mask written comes back on EVERY channel, not on none");

console.log("scripting.e2e.light_masks: all checks passed");
