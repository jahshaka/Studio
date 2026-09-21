// scripting.e2e.sky_name_recycle — A SKY CHANGE MUST NOT BURN A RESOURCE NAME
// (lane ENGINE-SMALL-C, SKYARRAY-LEAK-1; ledger §769).
//
// An equirect sky whose image was uploaded by the host (a colour sky, a
// gradient sky, any painted panorama) is copied into a one-slice 2D ARRAY
// because Ogre's SkyEquirectangular method is hard-gated on Type2DArray. That
// copy is a texture with a name, and the name used to come from the engine's
// PROCESS-UNIQUE counter: every sky application burned one for ever
// ("skyarray_0", "skyarray_1", ... "skyarray_209" in a 60-change script here),
// while the TEXTURE itself was destroyed immediately — a name leak, not a
// memory leak.
//
// It is not cosmetic. Ogre's v1 TextureUnitState (the sky material's) keeps the
// texture's NAME beside the pointer and nulls only the POINTER when the texture
// dies, so any later re-resolve of that unit looks the dead name up in the
// resource groups: "Cannot locate resource skyarray_NN", the layer latches
// `mTextureLoadFailed` and renders BLANK. That is what the owner's 2026-09-18
// session logged, twice per occurrence, with the index climbing past 130.
//
// THE ASSERTION is the name index itself, read through app.textureMemory():
// with the name recycled the live sky array sits in one of the FIRST few slots
// for ever, however many times the sky changes, and the process's texture count
// does not move.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

// The live sky-array textures, by name, and the highest slot index among them.
function skyArrays() {
    var t = app.textureMemory({ top: 0 });
    var names = [], worst = -1;
    for (var i = 0; i < t.entries.length; i++) {
        var n = t.entries[i].name;
        if (n.indexOf("skyarray_") !== 0) continue;
        names.push(n);
        var slot = parseInt(n.substring("skyarray_".length), 10);
        if (slot > worst) worst = slot;
    }
    return { names: names, worst: worst, count: t.count };
}

project.create("Sky name recycle " + Date.now());

// A colour sky is the cheapest host-uploaded equirect there is: a 64x32 strip.
world.sky("color", { color: { r: 10, g: 40, b: 200 } });
editor.frame(3);

var first = skyArrays();
console.log("after the first sky: " + JSON.stringify(first));
assert(first.names.length >= 1, "a host-uploaded equirect sky owns a sky-array copy");

// Sixty changes of colour and type — every one of them a fresh upload and a
// fresh array copy, with the previous one destroyed.
for (var k = 0; k < 60; k++) {
    if (k % 3 === 0)      world.sky("color", { color: { r: (k * 4) % 250, g: 40, b: 200 } });
    else if (k % 3 === 1) world.sky("gradient", { top: { r: (k * 7) % 250, g: 10, b: 90 } });
    else                  world.sky("realistic", { density: 0.2 + (k % 5) * 0.1 });
    editor.frame(2);
}
// Back to a host-uploaded sky, so there is a live array to look at.
world.sky("color", { color: { r: 200, g: 40, b: 10 } });
editor.frame(3);

var after = skyArrays();
console.log("after 60 more: " + JSON.stringify(after));
assert(after.names.length >= 1, "the sky still owns its array copy");
// THE NUMBER: a recycled pool never needs more slots than there are sky arrays
// alive at one moment (one per scene with a host-uploaded sky, and this session
// has the editor's). Eight is generous room for the editor's own scene plus any
// preview scene alive beside it; before the fix this read 209.
assert(after.worst < 8,
       "the sky-array name pool is RECYCLED: highest live slot " + after.worst + " after 61 skies");
// ...and no texture piled up either: the copies really were destroyed.
assert(after.count <= first.count,
       "no texture leak beside the name (" + first.count + " -> " + after.count + ")");

// The sky still draws: a blank sky layer is exactly what the dead-name path
// produced, so the picture is part of the assertion.
var shot = editor.screenshot("sky_name_recycle.png", 160, 120, [{ x: 0.5, y: 0.12 }], "plain");
var sky = shot.probes[0];
console.log("sky probe " + JSON.stringify(sky));
assert(sky.r + sky.g + sky.b > 0, "the sky is not black after 61 changes (the layer did not go blank)");

console.log("scripting.e2e.sky_name_recycle: all checks passed");
