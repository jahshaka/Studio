// script.reflector_attach_quiet — A MESH ATTACHED TO A REFLECTOR THAT CANNOT RE-ARM LEAVES NO ERROR (SCRIPT-IN-ENGINE-1 2f,
// the train's card: P14, the lead's ruling - a law no other row guards). Run by jahengine.
//
// THE LAW (irisgl engine/src/OgreMaterials.cpp, attachMesh's end: `if (!armReflector(id, n)) mError = before` - TRUE
// LEAVES NO ERROR, SCRIPT-IN-ENGINE-1 2e e1): a reflector node whose mesh is swapped re-derives its plane from the new
// geometry; when that fails the node simply stops reflecting - the REFLECTOR's state, never attachMesh's error: attachMesh
// answers true and leaves the engine's error sink EMPTY, so the record route publishes no refusal for it
// (OgreEngine::applySceneMessage publishes an accepted message that left an error as REFUSED) - a regression floods the
// app's error pump, because the mirror RETRIES its pushes on a refusal.
// NOT THIS ROW's LAW, AND WHY (the lead, 2026-11-23): a verb that disarms an armed reflector re-derives and leaves the
// reflector's refusal (CULL-TWIN-DEBTS-1: engine.planar_asan); the record route publishes it once; attachMesh restores
// because the mirror retries its pushes. So the fixture's setFaceCull below leaves "two-sided" in the sink (printed).
// THE FIXTURE: planar reflections at budget 1, a thin plate armed as the reflector (one active plane), its node made
// two-sided (engine.node.setFaceCull "twoSided": it wears its material's two-sided cull twin - the mirror disarms, active
// planes 1 -> 0), then a new mesh attached on the flagged two-sided node: its re-arm FAILS (planar::isTwoSided).
// THE LAW READ: attachMesh answers true, the engine's error sink is EMPTY after it (engine.takeLastError: the script host
// calls the engine directly, so the sink is where a left error lands - on the record route the same error is the
// published refusal), input.messagesRefused does not move, and the re-arm truly failed (active planes 0).
// THE CONTROL (the path really ran): the plate un-flagged and asked to arm again by engine.planar.setReflector is REFUSED
// in the reflector's own words ("two-sided") - answered false with them, or published as a refusal carrying them.
// RED ONCE (kept: spikes/script-in-engine-1/red-once/script.reflector_attach_quiet.restore_removed.txt): attachMesh's
// restore removed in a scratch engine arm - the attach's error left in the sink, FAILED (1).

const view = engine.view.createOffscreen({name: "reflquiet", width: 64, height: 64, background: [0, 0, 0, 1]});
const scene = engine.scene.create("reflquiet");
engine.view.setScene(view, {scene});
engine.scene.setAmbient(scene, {upper: [0.3, 0.3, 0.3, 1], lower: [0.3, 0.3, 0.3, 1]});
host.check({ok: engine.planar.set(scene, {budget: 1, resolution: 256}), what: "planar reflections at budget 1"});
const cube = engine.mesh.unitCube(scene);
const plate = engine.node.create(scene);
engine.node.attachMesh(scene, plate, {mesh: cube, material: engine.material.createPbr(scene, {roughness: 0.1, metalness: 1})});
engine.node.setTransform(scene, plate, {scale: [6, 0.02, 6]});
const box = engine.node.create(scene);
engine.node.attachMesh(scene, box, {mesh: cube, material: engine.material.createPbr(scene, {})});
engine.node.setTransform(scene, box, {position: [0, 1, -1]});
engine.view.lookAt(view, {eye: [0, 2, 4], target: [0, 0, 0]});
host.check({ok: engine.planar.setReflector(scene, plate, {on: true}), what: "the plate is armed as the reflector"});
engine.frames(3);

const refused = () => engine.published({select: ["input"]}).input.messagesRefused;
const facts = () => engine.scene.published(scene, {select: ["facts", "activePlanarReflectors"]});
const before = facts();
host.check({ok: before.activePlanarReflectors === 1, what: "the reflector is armed (active planes " +
            before.activePlanarReflectors + ", reflectorArms " + before.facts.reflectorArms + ")"});

// THE FIXTURE's step: the node two-sided (the mirror disarms; CULL-TWIN-DEBTS-1 leaves the reflector's refusal: printed)
engine.takeLastError();
engine.node.setFaceCull(scene, plate, "twoSided");
console.log("setFaceCull twoSided left (CULL-TWIN-DEBTS-1's design, not this row's law): '" + engine.takeLastError() + "'");
engine.frames(3);
const disarmed = facts(), refused0 = refused();
console.log("after it: active planes " + disarmed.activePlanarReflectors + ", reflectorArms " + disarmed.facts.reflectorArms);

// THE LAW: attachMesh on the flagged two-sided node
const material = engine.material.createPbr(scene, {roughness: 0.1, metalness: 1});
engine.takeLastError();                 // the engine's one error sink, emptied before the attach
const attached = engine.node.attachMesh(scene, plate, {mesh: cube, material});
const sinkAttach = engine.takeLastError();
engine.frames(3);
const after = facts(), refused1 = refused();
host.check({ok: attached === true && sinkAttach === "" && refused1 === refused0 && after.activePlanarReflectors === 0,
            what: "attachMesh on the flagged two-sided reflector: answered " + attached + ", the error it left '" + sinkAttach +
                  "', published refusals " + refused0 + " -> " + refused1 + "; its re-arm failed: active planes " +
                  after.activePlanarReflectors + " (reflectorArms " + after.facts.reflectorArms + ")"});

// THE CONTROL: the reflector's own refusal for this mesh, by its words (off first: a node already flagged answers true
// without measuring, OgrePlanar.cpp setNodePlanarReflector)
host.check({ok: engine.planar.setReflector(scene, plate, {on: false}), what: "the plate un-flagged"});
const armed = engine.planar.setReflector(scene, plate, {on: true});
const said = host.lastError();
engine.frames(1);
const pub = engine.published({select: ["refusals", "input"]});
const texts = (pub.refusals || []).map(r => r.text || JSON.stringify(r));
const published = pub.input.messagesRefused > refused1 && texts.some(t => t.includes("two-sided"));
host.check({ok: (armed === false && said.includes("two-sided")) || published,
            what: "THE CONTROL: arming the two-sided mesh is refused in the reflector's words (answer " + armed + ", '" + said +
                  "'; published " + (pub.input.messagesRefused - refused1) + " " + JSON.stringify(texts.slice(-1)) + ")"});
