// script.reflector_attach_quiet — A MESH ATTACHED TO AN ARMED REFLECTOR THAT CANNOT RE-ARM PUBLISHES NO REFUSAL
// (SCRIPT-IN-ENGINE-1 2f, the train's card: P14, the lead's ruling - a law no other row guards). Run by jahengine.
//
// THE LAW (irisgl engine/src/OgreMaterials.cpp, attachMesh's end): a reflector node whose mesh is swapped re-derives its
// plane from the new geometry; when that fails the node simply stops reflecting - the REFLECTOR's state (the published
// facts), never attachMesh's error: attachMesh answers true and leaves NO error (`mError = before`), so the record route
// publishes no refusal for it (OgreEngine::applySceneMessage publishes an accepted message that left an error as
// REFUSED: SCRIPT-IN-ENGINE-1 2e e1). A regression floods the app's error pump at every sync of such a node.
// THE FIXTURE: planar reflections at budget 1, a thin plate armed as the reflector (published reflectorArms 1), then its
// node made two-sided (engine.node.setFaceCull "twoSided": the node wears its material's TWO-SIDED cull twin) and a new
// mesh attached - the re-arm FAILS (planar::isTwoSided: a two-sided mirror fills its own reflection with itself).
// THE LAW READ: attachMesh answers true and leaves the engine's error sink EMPTY (engine.takeLastError: the script host
// calls the engine directly, so the sink is where a left error lands - on the record route the same error is the
// published refusal); the published refusal count (input.messagesRefused) does not move and reflectorArms reads 1 -> 1
// three frames later.
// THE CONTROL (the path really ran): the same node, un-flagged and asked to arm again by engine.planar.setReflector, is
// REFUSED in the reflector's own words ("two-sided") - answered false with them, or published as a refusal carrying them.
// RED ONCE (kept: spikes/script-in-engine-1/red-once/script.reflector_attach_quiet.restore_removed.txt): the restore
// removed in a scratch engine arm - the attach's error published, the count moves.

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
const arms = () => engine.scene.published(scene, {select: ["facts"]}).facts.reflectorArms;
const refused0 = refused(), arms0 = arms();
host.check({ok: arms0 === 1, what: "the reflector is armed before the swap (reflectorArms " + arms0 + ")"});

engine.node.setFaceCull(scene, plate, "twoSided");
const material = engine.material.createPbr(scene, {roughness: 0.1, metalness: 1});
const sink0 = engine.takeLastError();   // the engine's one error sink, emptied before the attach
const attached = engine.node.attachMesh(scene, plate, {mesh: cube, material});
const sink1 = engine.takeLastError();   // what the attach left in it (the script host calls the engine directly)
engine.frames(3);
const refused1 = refused(), arms1 = arms();
host.check({ok: attached === true && sink1 === "" && refused1 === refused0 && arms1 === arms0,
            what: "a two-sided mesh attached to the armed reflector: attachMesh " + attached + ", the error it left '" + sink1 +
                  "' (the sink before: '" + sink0 + "'), published refusals " + refused0 + " -> " + refused1 +
                  ", reflectorArms " + arms0 + " -> " + arms1});

// THE CONTROL: the reflector's own refusal for this mesh, by its words (off first: a node already flagged answers
// true without measuring, OgrePlanar.cpp setNodePlanarReflector)
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
