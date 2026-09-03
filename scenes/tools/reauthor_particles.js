// Re-authors scenes/Particles.zip onto the ParticleFX2 emitters
// (PARTICLES_FX2_SPEC.md §8 / §11 phase 3). Run it, don't hand-edit the archive:
//
//   cd <a scratch dir>
//   HOME=<scratch home> ./build-linux/bin/Jahshaka \
//       --script scenes/tools/reauthor_particles.js
//
// It imports the shipped archive, rebuilds the four emitters, re-exports over
// scenes/Particles.zip and re-shoots scenes/preview/particles.png. Idempotent:
// it always rebuilds the emitters from scratch, so running it twice gives the
// same scene.
//
// WHY THE EMITTERS ARE DELETED AND RE-ADDED rather than edited in place: the
// shipped sample has DUPLICATE NODE GUIDS. Its two "Fire" nodes share one guid
// and its two "Ash" nodes share another, because SceneEditService::
// addAssetParticleSystem gives a dragged-in emitter the LIBRARY ASSET's guid
// instead of a fresh node guid — so dropping one particle asset twice produces
// two nodes that no guid-addressed verb (scripting, MCP, the writer's per-node
// DB row) can tell apart. Fresh nodes get fresh guids. The defect in
// addAssetParticleSystem is reported separately; this only fixes the data.
//
// The recipe, per the spec: fire + smoke on the fire pit, fire + embers on the
// sphere. PFX2 has no sub-emitters, so embers really are a second node.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var REPO = "@REPO@";
var ARCHIVE = REPO + "/scenes/Particles.zip";

var imported = project.importArchive(ARCHIVE);
assert(imported && imported.guid, "imported " + ARCHIVE + " -> " + imported.guid);
assert(project.open(imported.guid), "opened the project");

// ---- find what we keep, and the two particle images -----------------------
function nodeByName(name, type) {
    var ns = scene.nodes();
    for (var i = 0; i < ns.length; i++)
        if (ns[i].name === name && (!type || ns[i].type === type)) return ns[i];
    return null;
}
var sphere = nodeByName("Sphere", "mesh");
var pit    = nodeByName("Fire Pit", "mesh");
assert(sphere && pit, "the Sphere and the Fire Pit are where they were");

var fireTex = "", smokeTex = "";
var lib = assets.list({ scope: "project", type: "texture" });
for (var i = 0; i < lib.length; i++) {
    if (lib[i].name === "Fire2.png")  fireTex = lib[i].guid;
    if (lib[i].name === "smoke.png")  smokeTex = lib[i].guid;
}
assert(fireTex && smokeTex, "the sample's Fire2.png and smoke.png are in the project");

// ---- out with the old ------------------------------------------------------
// Four emitters, two addressable guids: remove by guid until none is left.
var removed = 0;
for (var pass = 0; pass < 8; pass++) {
    var ns = scene.nodes(), hit = null;
    for (var j = 0; j < ns.length; j++) if (ns[j].type === "particles") { hit = ns[j].id; break; }
    if (!hit) break;
    if (node.remove(hit)) removed++;
}
console.log("removed " + removed + " legacy emitter guid(s)");
var left = 0;
var ns0 = scene.nodes();
for (var k = 0; k < ns0.length; k++) if (ns0[k].type === "particles") left++;
assert(left === 0, "every legacy emitter is gone");

// ---- in with the new -------------------------------------------------------
// The fire pit's bowl sits around y = 2.4; the sphere floats at y = 4.
function makeEmitter(name, preset, parentId, pos, texGuid, tweaks) {
    var id = scene.addParticles(preset, { parent: parentId, position: pos });
    assert(id && id.length > 10, name + ": created (" + preset + ")");
    assert(node.setProperty(id, "name", name), name + ": named");
    assert(node.setParticleTexture(id, texGuid), name + ": particle image bound");
    if (tweaks) for (var key in tweaks) assert(node.setProperty(id, key, tweaks[key]), name + "." + key);
    return id;
}

// The pit: a flame in the bowl, smoke leaving above it.
// Sizes are in WORLD units and this pit is a big prop — the default camera sits
// 16 m back and 11 m up, so a 0.35 m flame quad is three pixels. The preset is
// the recipe; the scale is the set dressing.
var pitFire = makeEmitter("Fire", "fire", pit.id, { x: 0.04, y: 2.5, z: 0.04 }, fireTex, {
    particlesPerSecond: 200,
    particleScale: 1.1,
    speed: 3.4,
    lifeLength: 1.7,
    coneAngle: 20,
    maxParticles: 2048
});
var pitSmoke = makeEmitter("Smoke", "smoke", pit.id, { x: 0.0, y: 4.6, z: 0.0 }, smokeTex, {
    particlesPerSecond: 14,
    particleScale: 2.4,
    speed: 1.6,
    lifeLength: 5.0,
    maxParticles: 512
});

// The sphere: a smaller flame, plus embers streaking off it. Embers are their
// own node because ParticleFX2 has no sub-emitters — a particle cannot emit.
var sphereFire = makeEmitter("Fire", "fire", sphere.id, { x: 0, y: -0.5, z: 0 }, fireTex, {
    particlesPerSecond: 140,
    particleScale: 0.75,
    speed: 2.6,
    lifeLength: 1.2,
    maxParticles: 1024
});
var sphereEmbers = makeEmitter("Embers", "embers", sphere.id, { x: 0, y: -0.2, z: 0 }, fireTex, {
    particlesPerSecond: 22,
    particleScale: 0.18,
    speed: 4.0,
    lifeLength: 2.6,
    maxParticles: 256
});

// ---- the other half of "real fire" ----------------------------------------
// Every emissive preset carries HDR colour keys (values above 1). Without HDR
// in the view's post chain they clamp and the flame reads as a flat yellow
// sticker instead of as light, so the sample turns HDR on. It is a World MODE
// row, hence world.override.
console.log("hdr: " + JSON.stringify(world.override({ id: "hdr", value: true })));

// BLOOM IS DELIBERATELY OFF, against the spec's recommendation, and the
// evidence is in scenes/preview/. With bloom on, this flame paints a solid
// orange band straight across the ENTIRE frame at its own height — a smear
// that is horizontal only, so the bright pass's separable blur is not
// symmetric. It is a post-chain defect, not a particle one (the flame itself is
// identical in both shots), and it makes the sample look worse rather than
// better. Turn this back on when that is fixed: the HDR values are already
// there waiting for it.
console.log("bloom: " + JSON.stringify(world.override({ id: "bloom", value: false })));

assert(project.save(), "saved");

// ---- the preview shot ------------------------------------------------------
// Game View first: the shipped thumbnail should be the SCENE, not the editor —
// no gizmo, no selection outline, no ground grid. Then let the plumes fill,
// because the engine simulates them and an unwarmed emitter photographs as an
// empty scene.
editor.select(null);
editor.gameView(true);
editor.frame(240);
var shot = editor.screenshot(REPO + "/scenes/preview/particles.png", 480, 270, [], true);
console.log("preview: " + JSON.stringify(shot.center));
editor.gameView(false);

// ---- back out to the archive ----------------------------------------------
var out = project.exportArchive(ARCHIVE);
assert(out && out.assets > 0 && out.objects > 0,
       "re-exported " + ARCHIVE + " (" + out.assets + " assets, " + out.objects + " objects)");
assert(project.close(), "closed");
console.log("reauthor_particles: PASS");
