// script.reflector_attach_quiet — A VERB THAT SUCCEEDS ON AN ARMED REFLECTOR's NODE LEAVES NO ERROR, EVEN WHEN THE
// REFLECTOR CANNOT RE-ARM (SCRIPT-IN-ENGINE-1 2f, the train's card: P14 and its class, the lead's rulings - a law no
// other row guards). Run by jahengine.
//
// THE LAW (TRUE LEAVES NO ERROR, SCRIPT-IN-ENGINE-1 2e e1): a node that is a planar reflector re-derives its plane when its
// geometry or the cull it wears changes; when the re-arm fails the node simply stops reflecting - the REFLECTOR's state,
// never the verb's error: the verb answers success and leaves the engine's error sink as it was (OgreMaterials.cpp
// attachMesh's `mError = before`; OgrePlanar.cpp rederiveReflector and rebuildPlanar the same), so the record route
// publishes no refusal for it (OgreEngine::applySceneMessage publishes an accepted message that left an error as REFUSED).
// A regression floods the app's error pump at every sync of such a node.
// THE FIXTURE: planar reflections at budget 1, a thin plate armed as the reflector (one active plane), then
//   (1) engine.node.setFaceCull(plate, "twoSided"): the node wears its material's TWO-SIDED cull twin and the re-arm FAILS
//       (planar::isTwoSided: a two-sided mirror fills its own reflection with itself) - the arm is truly lost: the active
//       planes read 1 -> 0;
//   (2) engine.node.attachMesh on the same (flagged, two-sided) node: its re-arm fails the same way.
// THE LAW READ, for each verb: it succeeds, the engine's error sink is EMPTY after it (engine.takeLastError: the script host
// calls the engine directly, so the sink is where a left error lands - on the record route the same error is the published
// refusal), and the published refusal count (input.messagesRefused) does not move. reflectorArms is printed (what it reads).
// THE CONTROL (the path really ran): the same node, un-flagged and asked to arm again by engine.planar.setReflector, is
// REFUSED in the reflector's own words ("two-sided") - answered false with them, or published as a refusal carrying them.
// RED ONCE (kept): attachMesh's restore removed in a scratch engine arm
// (spikes/script-in-engine-1/red-once/script.reflector_attach_quiet.restore_removed.txt) and setFaceCull at the tip before
// its fix (script.reflector_attach_quiet.face_cull_before_fix.txt) - the verb's error left in the sink.

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
const before = facts(), refused0 = refused();
host.check({ok: before.activePlanarReflectors === 1, what: "the reflector is armed before the verbs (active planes " +
            before.activePlanarReflectors + ", reflectorArms " + before.facts.reflectorArms + ")"});

// (1) setFaceCull "twoSided" on the armed reflector's node
engine.takeLastError();                 // the engine's one error sink, emptied before the verb
engine.node.setFaceCull(scene, plate, "twoSided");
const sinkCull = engine.takeLastError();
engine.frames(3);
const afterCull = facts(), refusedCull = refused();
host.check({ok: sinkCull === "" && refusedCull === refused0 && afterCull.activePlanarReflectors === 0,
            what: "(1) setFaceCull twoSided on the armed reflector: the error it left '" + sinkCull + "', published refusals " +
                  refused0 + " -> " + refusedCull + "; the arm truly lost: active planes " + before.activePlanarReflectors +
                  " -> " + afterCull.activePlanarReflectors + " (reflectorArms " + before.facts.reflectorArms + " -> " +
                  afterCull.facts.reflectorArms + ")"});

// (2) attachMesh on the same flagged, two-sided node
const material = engine.material.createPbr(scene, {roughness: 0.1, metalness: 1});
engine.takeLastError();
const attached = engine.node.attachMesh(scene, plate, {mesh: cube, material});
const sinkAttach = engine.takeLastError();
engine.frames(3);
const afterAttach = facts(), refusedAttach = refused();
host.check({ok: attached === true && sinkAttach === "" && refusedAttach === refusedCull,
            what: "(2) attachMesh on the flagged two-sided reflector: answered " + attached + ", the error it left '" + sinkAttach +
                  "', published refusals " + refusedCull + " -> " + refusedAttach + " (active planes " +
                  afterAttach.activePlanarReflectors + ", reflectorArms " + afterAttach.facts.reflectorArms + ")"});

// THE CONTROL: the reflector's own refusal for this mesh, by its words (off first: a node already flagged answers true
// without measuring, OgrePlanar.cpp setNodePlanarReflector)
host.check({ok: engine.planar.setReflector(scene, plate, {on: false}), what: "the plate un-flagged"});
const armed = engine.planar.setReflector(scene, plate, {on: true});
const said = host.lastError();
engine.frames(1);
const pub = engine.published({select: ["refusals", "input"]});
const texts = (pub.refusals || []).map(r => r.text || JSON.stringify(r));
const published = pub.input.messagesRefused > refusedAttach && texts.some(t => t.includes("two-sided"));
host.check({ok: (armed === false && said.includes("two-sided")) || published,
            what: "THE CONTROL: arming the two-sided mesh is refused in the reflector's words (answer " + armed + ", '" + said +
                  "'; published " + (pub.input.messagesRefused - refusedAttach) + " " + JSON.stringify(texts.slice(-1)) + ")"});
