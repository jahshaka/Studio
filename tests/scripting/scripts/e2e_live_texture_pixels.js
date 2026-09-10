// scripting.e2e.live_texture_pixels — a live texture REACHES THE SCREEN
// (MATERIAL_GAPS_SPEC ADDENDUM A-1).
//
// The document half is scripting.e2e.live_textures' job. This is the half no
// document assertion can stand in for: a live texture bound as a base colour
// map must actually be sampled by the renderer, and a WRITE must change what
// is drawn — with no re-binding, no material edit and no engine verb of its
// own, purely because the generation moved and SceneMirror noticed.
//
// It also pins the other half of that claim, the one a still image hides: the
// engine's texture COUNT must not move across a run of writes. A push that
// re-created the texture per write would render identically and leak a texture
// a frame.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function show(tag, c) { return tag + " (" + c.r + "," + c.g + "," + c.b + ")"; }
function dist(a, b) {
    return Math.abs(a.r - b.r) + Math.abs(a.g - b.g) + Math.abs(a.b - b.b);
}

var guid = project.create("Live Pixels " + Date.now());
assert(guid.length > 10, "project.create");

// A bare primitive: its default material is an untextured PbrMaterial, so the
// only thing that can colour it is the map under test.
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
var tex = texture.createLive("solid", 16, 16);

assert(material.set(cube, { baseColorMap: tex, metallic: 0, roughness: 0.55 }) === true,
       "the live texture binds as baseColorMap");

function probe(name) {
    editor.select(null);   // a selected node wears a gizmo over the centre pixel
    editor.frameNode(cube, { yaw: 25, pitch: 20, distance: 3.2 });
    editor.frame(3);
    return editor.screenshot(name, 128, 128, [{ x: 0.5, y: 0.5 }]).probes[0];
}

// ---- 1. black at birth, and it is the TEXTURE that is being sampled -----
var born = probe("live_born.png");
console.log("    " + show("BORN", born));
assert(born.r < 60 && born.g < 60 && born.b < 60,
       "a freshly created live texture is opaque black on the surface " + show("", born));

// ---- 2. a write changes the pixels, with no re-bind --------------------
assert(texture.write(tex, 16, 16, "#ff0000") === true, "write red");
var red = probe("live_red.png");
console.log("    " + show("RED", red));
assert(red.r > red.b + 40 && red.r > red.g + 40,
       "the surface is RED after the write " + show("", red));
assert(red.r > born.r + 40, "and much brighter than the black it replaced");

assert(texture.write(tex, 16, 16, "#0000ff") === true, "write blue");
var blue = probe("live_blue.png");
console.log("    " + show("BLUE", blue));
assert(blue.b > blue.r + 40 && blue.b > blue.g + 40,
       "the SAME texture now draws BLUE " + show("", blue));

// ---- 3. a byte array is pixels too, not only a fill --------------------
var bytes = [];
for (var p = 0; p < 16 * 16; ++p) bytes.push(20, 220, 20, 255);
assert(texture.write(tex, 16, 16, bytes) === true, "write a green byte array");
var green = probe("live_green.png");
console.log("    " + show("GREEN", green));
assert(green.g > green.r + 40 && green.g > green.b + 40,
       "the surface is GREEN " + show("", green));

// ---- 4. writes REUSE the engine texture, they do not recreate it -------
var before = app.engineObjects().textures;
for (var w = 0; w < 6; ++w) {
    assert(texture.write(tex, 16, 16, w % 2 ? "#ff00ff" : "#00ffff") === true,
           "write " + w);
    editor.frame(1);
}
var after = app.engineObjects().textures;
assert(after === before,
       "six writes created no new engine texture (" + before + " -> " + after + ")");
assert(texture.info(tex).generation === 10,
       "ten generations: birth, red, blue, green, and six more");

// ---- 5. and the last one is on screen ----------------------------------
var last = probe("live_last.png");
console.log("    " + show("LAST", last));
assert(last.r > last.g + 30 && last.b > last.g + 30,
       "the last write (magenta) is what is drawn " + show("", last));

// ---- 6. removing it under a live binding does not crash ----------------
// And it does not blank the surface either: the renderer's copy of the last
// pixels lives until nothing binds it, so what stops is the UPDATING. That is
// the documented behaviour of texture.remove and it is asserted here rather
// than described.
assert(texture.remove(tex) === true, "texture.remove while bound");
editor.frame(3);
var gone = probe("live_gone.png");
console.log("    " + show("GONE", gone));
assert(scene.nodes().length >= 1, "the scene still renders with a dead reference");
assert(dist(gone, last) <= 6,
       "the surface keeps the last pixels it was given (" + dist(gone, last) + ")");
assert(material.set(cube, { baseColorMap: "" }) === true, "clearing the map is what blanks it");
editor.frame(3);
var cleared = probe("live_cleared.png");
console.log("    " + show("CLEARED", cleared));
assert(dist(cleared, gone) > 20, "and it IS a different picture (" + dist(cleared, gone) + ")");

console.log("e2e live texture pixels: all assertions passed");
