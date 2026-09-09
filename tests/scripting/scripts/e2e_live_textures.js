// scripting.e2e.live_textures — LIVE TEXTURES end to end (MATERIAL_GAPS_SPEC
// ADDENDUM A-1, owner decision 2026-09-09: a session-only asset kind).
//
// What this pins, and why each one:
//   1. the producer verbs exist and answer honestly — createLive/write/info/
//      list/destroy, with the generation counter moving exactly once per
//      accepted write (that counter is the whole synchronisation contract with
//      the renderer, so a test that does not watch it tests nothing);
//   2. the three pixel spellings (colour fill, byte array, base64) all land,
//      and a wrong size / wrong guid is REFUSED rather than stretched;
//   3. material.set resolves a live guid like any other texture guid — the
//      point of the identity — and stores the session reference;
//   4. THE WRITER SKIPS IT: saving a scene that binds a live texture must not
//      put its guid in the file, because nothing could ever resolve it again.
//      Reopening comes back with an empty map and no crash, which is the
//      documented cost of the design, asserted rather than assumed;
//   5. destroy removes the identity and the pixels, and is idempotent-safe.
//
// Document verbs only -> --headless.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function mustThrow(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, msg);
}

var guid = project.create("Live Textures " + Date.now());
assert(guid.length > 10, "project.create");

// ---- 1. create ----------------------------------------------------------
var tex = texture.createLive("scripted", 4, 4);
assert(typeof tex === "string" && tex.length > 10, "texture.createLive returns a guid");

var info = texture.info(tex);
assert(info.width === 4 && info.height === 4, "info reports the size it was created with");
assert(info.name === "scripted", "info reports the name");
assert(info.mipmaps === false, "mipmaps default off");
assert(info.generation === 1, "a fresh live texture is at generation 1 (its black birth frame)");
assert(info.ref === "live://" + tex, "the reference form is live://<guid>");

var listed = texture.list();
var seen = false;
for (var i = 0; i < listed.length; ++i) if (listed[i].guid === tex) seen = true;
assert(seen, "texture.list contains it");

// The session catalog is the ONLY place it exists: no store row, ever.
var storeRows = assets.list({ type: "texture" });
for (var s = 0; s < storeRows.length; ++s)
    assert(storeRows[s].guid !== tex, "a live texture is never a library row");

// ---- 2. the three pixel spellings, and the refusals ---------------------
assert(texture.write(tex, 4, 4, "#ff0000") === true, "write a colour fill");
assert(texture.info(tex).generation === 2, "the generation moved by exactly one");

var bytes = [];
for (var p = 0; p < 4 * 4; ++p) bytes.push(0, 128, 255, 255);
assert(texture.write(tex, 4, 4, bytes) === true, "write a byte array");
assert(texture.info(tex).generation === 3, "and again by exactly one");

// base64 of the same 64 bytes: 0x00 0x80 0xff 0xff repeated.
var b64 = "AID//wCA//8AgP//AID//wCA//8AgP//AID//wCA//8AgP//AID//wCA//8AgP//AID//wCA//8AgP//AID//w==";
assert(texture.write(tex, 4, 4, b64) === true, "write a base64 payload");
assert(texture.info(tex).generation === 4, "and again by exactly one");

mustThrow(function () { texture.write(tex, 8, 8, "#00ff00"); },
          "a size that is not the texture's is refused (it cannot resize)");
mustThrow(function () { texture.write(tex, 4, 4, [1, 2, 3]); },
          "a short pixel array is refused");
mustThrow(function () { texture.write(tex, 4, 4, {}); },
          "pixels that are neither array, base64 nor colour are refused");
mustThrow(function () { texture.write(tex, 4, 4, "#ff0000", { mips: true }); },
          "mips on a WRITE is refused — mip levels are fixed at creation");
assert(texture.write("not-a-guid", 4, 4, "#ff0000") === false,
       "writing an unknown guid answers false (a refusal, not a throw)");
assert(texture.info("not-a-guid") === null, "info on an unknown guid is null");
assert(texture.info(tex).generation === 4,
       "not one refusal moved the generation");

// A mipmapped one, because the flag lives on the texture and not on the write.
var mipped = texture.createLive("mipped", 8, 8, { mipmaps: true });
assert(texture.info(mipped).mipmaps === true, "createLive({mipmaps:true}) is recorded");
mustThrow(function () { texture.createLive("bad", 0, 8); }, "a zero dimension is refused");
mustThrow(function () { texture.createLive("huge", 99999, 8); }, "an absurd size is refused");

// ---- 3. material.set resolves the guid like any other texture -----------
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(material.set(cube, { baseColorMap: tex }) === true,
       "material.set(node, {baseColorMap: <live guid>})");
assert(material.get(cube).baseColorMap === "live://" + tex,
       "the row stores the session reference, not a path");
assert(material.properties(cube).writableKeys.indexOf("reflectionMap") >= 0,
       "reflectionMap is a writable key (ADDENDUM A-5)");
mustThrow(function () { material.set(cube, { reflectionMap: tex }); },
          "a live texture is refused for reflectionMap (a cube is built once, it cannot follow a generation)");

// ---- 4. the writer skips it; a reopen does not crash --------------------
assert(project.save() === true, "project.save");
assert(project.close() === true, "project.close");
assert(project.open(guid) === true, "project.open");

var reMap = material.get(cube).baseColorMap;
assert(reMap === "" || reMap === undefined || reMap === null,
       "the saved scene carries NO live guid — the map comes back absent, got '" + reMap + "'");
// And the material is otherwise intact: a skipped map is not a broken material.
assert(material.get(cube).roughness !== undefined, "the material survived the round trip");
assert(scene.nodes().length >= 1, "the scene reopened");

// The texture itself is still in the session (the project boundary does not
// own it) — which is what makes it re-bindable after a reopen.
assert(texture.info(tex) !== null, "the live texture outlives the project it was bound in");
assert(material.set(cube, { baseColorMap: tex }) === true, "and can be bound again");

// ---- 5. destroy ---------------------------------------------------------
assert(texture.remove(mipped) === true, "texture.remove");
assert(texture.info(mipped) === null, "the identity is gone");
assert(texture.remove(mipped) === false, "destroying it twice answers false");
assert(texture.remove(tex) === true, "destroy the bound one too");
// A material still holding the reference is not a crash: the slot resolves to
// nothing, exactly like a file that was deleted underneath it.
assert(material.get(cube).baseColorMap === "live://" + tex,
       "the material keeps the reference string it was told");
assert(material.set(cube, { baseColorMap: "" }) === true, "and the slot can be cleared");

console.log("e2e live textures: all assertions passed");
