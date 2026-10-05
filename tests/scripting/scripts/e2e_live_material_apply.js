// scripting.e2e.live_material_apply — A LIVE GRAPH MATERIAL STAYS LIVE WHEN IT
// IS APPLIED FROM THE LIBRARY (TORNADO-1).
//
// graph.toMaterial builds the material from the graph in hand; an apply from the
// library (drag, material.apply) reads the material's stored DEFINITION through
// MaterialReader. The generated shader pieces travel with that definition
// (bake.emittedSockets + the graph, re-emitted, content-addressed) — or the
// material would arrive frozen at its t=0 bake. Saved, applied to a fresh cube,
// the material must carry its pieces, name its source, and animate:
// world.shaderTime 1.0 and 0 draw different frames.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

assert(project.create("Live Apply " + Date.now()).length > 10, "project.create");

// Base Color = lerp(blue, white, pulsate(pi/2)); Emissive = a pulsating orange.
var guid = materials.create("LiveApply " + Date.now(), { graph: true });
var master = null;
graph.nodes().forEach(function (n) { if (n.master) master = n.id; });
var mix = graph.addNode("lerp");
var blue = graph.addNode("color"), white = graph.addNode("color");
assert(graph.setValue(blue, { r: 0.1, g: 0.2, b: 0.9, a: 1 }) && graph.setValue(white, { r: 1, g: 1, b: 1, a: 1 }), "colours");
var pulse = graph.addNode("pulsate"), rate = graph.addNode("float");
assert(graph.setValue(rate, Math.PI / 2), "rate");
assert(graph.connect(rate, 0, pulse, 0) && graph.connect(blue, 0, mix, 0) && graph.connect(white, 0, mix, 1)
       && graph.connect(pulse, 0, mix, 2) && graph.connect(mix, 0, master, "Base Color"), "the pulse");
var glow = graph.addNode("multiply"), orange = graph.addNode("color");
assert(graph.setValue(orange, { r: 1, g: 0.5, b: 0.035, a: 1 }), "orange");
assert(graph.connect(orange, 0, glow, 0) && graph.connect(pulse, 0, glow, 1)
       && graph.connect(glow, 0, master, "Emissive"), "the glow");
assert(graph.emitInfo().route === "live", "the graph is LIVE");
assert(graph.save(), "graph.save writes the definition");

// ---- from the LIBRARY onto a fresh cube (the drag path's verb)
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(material.apply(cube, guid), "material.apply(cube, <library graph material>)");
var m = material.get(cube);
assert(typeof m.customPiecePixel === "string" && m.customPiecePixel.length > 0,
       "the applied material carries its generated pixel piece: " + m.customPiecePixel);
assert(materials.open(guid) && graph.emitInfo().route === "live", "graph.emitInfo on the applied material: live");

function frameAt(t, name) {
    assert(world.shaderTime({ t: t }).pinned, "pinned at " + t);
    editor.select(null);
    editor.frameNode(cube, { yaw: 25, pitch: 20, distance: 3.2 });
    editor.frame(30);
    return editor.screenshot(name, 96, 96, [{ x: 0.5, y: 0.5 }, { x: 0.45, y: 0.55 }, { x: 0.55, y: 0.45 }]).probes;
}
var a = frameAt(1.0, "live_apply_1.png"), b = frameAt(0.0, "live_apply_0.png");
var worst = 0;
for (var i = 0; i < a.length; ++i)
    worst = Math.max(worst, Math.abs(a[i].r - b[i].r), Math.abs(a[i].g - b[i].g), Math.abs(a[i].b - b[i].b));
console.log("    t=1 vs t=0 worst " + worst + " " + J(a[0]) + " " + J(b[0]));
assert(worst >= 20, "the applied material ANIMATES: t=1.0 and t=0 differ (worst " + worst + ")");

// ---- and it survives the scene round trip by its own definition
assert(project.save(), "project.save");
world.shaderTime({ t: null });
assert(project.close(), "project.close");
console.log("live_material_apply e2e: done");
