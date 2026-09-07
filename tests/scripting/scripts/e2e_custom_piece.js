// scripting.e2e.custom_piece — a GENERATED SHADER PIECE, end to end through
// the verbs in the real binary (HLMS_ADOPTION P5 §7.9).
//
// What this proves that the unit suites cannot: that the whole chain holds in
// the shipping app — emitter -> content-addressed cache file -> document
// material -> scene mirror -> renderer — and that it SURVIVES A ROUND TRIP.
// Save the project, close it, open it again: the piece file is named by a hash
// of its own bytes, so re-opening re-emits the identical name and the material
// comes back live rather than silently reverting to the baker's frozen t=0
// fold.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var projectGuid = project.create("Custom Piece " + Date.now());
assert(projectGuid.length > 10, "project.create");

// ---- a graph the emitter should take: an animated Base Color ----------------
var shaderGuid = materials.createGraph("PulsingFx");
assert(shaderGuid.length > 10, "materials.createGraph");

var masterId = null;
graph.nodes().forEach(function (n) { if (n.master) masterId = n.id; });
assert(masterId !== null, "the new graph has a master node");

// lerp(colour, white, pulsate(3)) -> Base Color
var mix = graph.addNode("lerp");
var colA = graph.addNode("color");
var colB = graph.addNode("color");
assert(graph.setValue(colA, { r: 0.2, g: 0.4, b: 0.9, a: 1.0 }), "colour A");
assert(graph.setValue(colB, { r: 1.0, g: 1.0, b: 1.0, a: 1.0 }), "colour B");
var pulse = graph.addNode("pulsate");
var rate = graph.addNode("float");
assert(graph.setValue(rate, 3.0), "pulse rate");
assert(graph.connect(rate, 0, pulse, 0), "rate -> pulsate");
assert(graph.connect(colA, 0, mix, 0), "A -> lerp");
assert(graph.connect(colB, 0, mix, 1), "B -> lerp");
assert(graph.connect(pulse, 0, mix, 2), "pulsate -> lerp T");
assert(graph.connect(mix, 0, masterId, "Base Color"), "lerp -> Base Color");

// ---- what the emitter makes of it ------------------------------------------
var emit = graph.emitInfo();
assert(emit.accepted === true, "graph.emitInfo: the emitter accepts the graph");
assert(emit.animated === true, "graph.emitInfo: reported animated");
assert(emit.emitted.indexOf("Base Color") !== -1, "graph.emitInfo: Base Color is emitted");
assert(emit.pixelSource.indexOf("@piece( custom_ps_preLights )") === 0 ||
       emit.pixelSource.indexOf("custom_ps_preLights") !== -1,
       "the emitted source targets custom_ps_preLights");
assert(emit.pixelSource.indexOf("passBuf.jahClock.x") !== -1,
       "the emitted source reads the pass-buffer clock");
assert(emit.ops.length >= 45, "the emitter reports its op vocabulary (" + emit.ops.length + " ops)");
assert(emit.vertexSource === "", "no vertex piece for a surface-only graph");

// The baker's own classification is unchanged and still honest: a pulsating
// colour is a CONSTANT to it (its t=0 sample) — which is the whole reason the
// piece exists.
var info = graph.bakeInfo().perSocket;
assert(info["Base Color"] === "uniform", "bakeInfo: the baker still sees a uniform fold");

// ---- apply it to a real mesh node ------------------------------------------
var cube = scene.addPrimitive("cube");
assert(cube.length > 0, "scene.addPrimitive");
assert(graph.toMaterial(cube), "graph.toMaterial applies the graph to the cube");

var projectFolder = project.current().folder;
var mat = material.get(cube);
assert(typeof mat.customPiecePixel === "string" && mat.customPiecePixel.length > 0,
       "material.get reports a generated pixel piece: " + mat.customPiecePixel);
var piecePath = mat.customPiecePixel;
assert(piecePath.indexOf("ShaderPieces") !== -1,
       "the piece lives in the per-user ShaderPieces cache, not in the project");
assert(piecePath.indexOf(".piece_ps.glsl") === piecePath.length - ".piece_ps.glsl".length,
       "the piece file is named <hash>.piece_ps.glsl");
assert(piecePath.indexOf(projectFolder) !== 0,
       "the piece is NOT inside the project folder (a missing piece must never be able " +
       "to invalidate another project's shader cache)");

// (The renderer hop is NOT asserted here: a --script run never presents a
// frame, so nothing has been mirrored into the engine scene yet and
// material.dumpDatablock correctly refuses. pieces.mirror renders that half.)

// ---- save, close, reopen: the piece must come back --------------------------
assert(graph.save(), "graph.save writes the emitted definition back to the asset");
assert(project.save(), "project.save");
assert(project.close(), "project.close");
assert(project.open(projectGuid), "project.open");

var reopened = null;
scene.nodes().forEach(function (n) { if (n.type === "mesh") reopened = n.id; });
assert(reopened !== null, "the cube came back");
var mat2 = material.get(reopened);
assert(typeof mat2.customPiecePixel === "string" && mat2.customPiecePixel.length > 0,
       "the reopened material still carries a generated piece");
assert(mat2.customPiecePixel === piecePath,
       "...and it is the SAME content-addressed file: re-emitting a graph is deterministic");

console.log("custom_piece e2e: done");
