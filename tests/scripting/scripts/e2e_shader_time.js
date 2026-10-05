// scripting.e2e.shader_time — THE SHADER CLOCK IS REPRODUCIBLE (TORNADO-1, G7).
//
// An animated graph material reads one scene-wide shader clock. The free clock
// counts the scene's own simulation steps (1/60 s each, never the wall clock);
// world.shaderTime({t}) PINS it, which is what makes a frame of an animated
// surface a function of t and nothing else:
//
//   * t = 0 and t = 1.0 draw DIFFERENT frames (the surface really animates),
//   * t = 1.0, then t = 0, then t = 1.0 again draws the FIRST frame again,
//     probe for probe (the same t is the same picture),
//   * {t: null} hands the clock back, a negative t is refused, and the verb
//     reports whether anything in the scene reads the clock at all.
//
// The surface: Base Color = lerp(blue, white, pulsate(pi/2)) — half-way at
// t = 0 (sin 0 = 0 -> 0.5), white at t = 1 (sin pi/2 = 1).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var projectGuid = project.create("Shader Time " + Date.now());
assert(projectGuid.length > 10, "project.create");

var cube = scene.addPrimitive("cube", { position: { x: 0, y: 1, z: 0 } });
assert(cube.length > 0, "scene.addPrimitive");

// Nothing reads the clock yet: the verb says so (and still answers).
var idle = world.shaderTime();
assert(idle.live === false, "a scene with no animated material reads no shader clock: " + J(idle));

var shaderGuid = materials.create("ShaderTimePulse", { graph: true });
assert(shaderGuid.length > 10, "materials.create");
var masterId = null;
graph.nodes().forEach(function (n) { if (n.master) masterId = n.id; });
var mix = graph.addNode("lerp");
var blue = graph.addNode("color");
var white = graph.addNode("color");
assert(graph.setValue(blue, { r: 0.1, g: 0.2, b: 0.9, a: 1.0 }), "blue");
assert(graph.setValue(white, { r: 1.0, g: 1.0, b: 1.0, a: 1.0 }), "white");
var pulse = graph.addNode("pulsate");
var rate = graph.addNode("float");
assert(graph.setValue(rate, Math.PI / 2), "pulse rate pi/2");
assert(graph.connect(rate, 0, pulse, 0), "rate -> pulsate");
assert(graph.connect(blue, 0, mix, 0), "blue -> lerp A");
assert(graph.connect(white, 0, mix, 1), "white -> lerp B");
assert(graph.connect(pulse, 0, mix, 2), "pulsate -> lerp T");
assert(graph.connect(mix, 0, masterId, "Base Color"), "lerp -> Base Color");
assert(graph.emitInfo().route === "live", "the graph reads the clock: the LIVE route");
assert(graph.toMaterial(cube), "graph.toMaterial");

// A 5x5 probe grid over the cube's face: the frames are compared probe by probe.
var grid = [];
for (var gy = 0; gy < 5; ++gy)
    for (var gx = 0; gx < 5; ++gx) grid.push({ x: 0.3 + 0.1 * gx, y: 0.3 + 0.1 * gy });

function frameAt(t, name) {
    var r = world.shaderTime({ t: t });
    assert(r.pinned === true && Math.abs(r.t - t) < 1e-6, "pinned at t = " + t + ": " + J(r));
    editor.select(null);   // a selected node wears a gizmo
    editor.frameNode(cube, { yaw: 25, pitch: 20, distance: 3.2 });
    editor.frame(30);      // settle (exposure, history) — frames, not seconds
    return editor.screenshot(name, 128, 128, grid).probes;
}
function differences(a, b) {
    var n = 0, worst = 0;
    for (var i = 0; i < a.length; ++i) {
        var d = Math.max(Math.abs(a[i].r - b[i].r), Math.abs(a[i].g - b[i].g), Math.abs(a[i].b - b[i].b));
        if (d > 0) ++n;
        worst = Math.max(worst, d);
    }
    return { probes: n, worst: worst };
}

var first = frameAt(1.0, "shader_time_1a.png");
assert(world.shaderTime().live === true, "the scene now reads the shader clock");
var zero = frameAt(0.0, "shader_time_0.png");
var again = frameAt(1.0, "shader_time_1b.png");

var moved = differences(first, zero);
console.log("    t=1 vs t=0: " + J(moved));
assert(moved.probes >= 10 && moved.worst >= 20, "t = 0 and t = 1.0 draw DIFFERENT frames " + J(moved));
var same = differences(first, again);
console.log("    t=1 vs t=1: " + J(same));
assert(same.probes === 0, "t = 1.0 twice draws the IDENTICAL frame " + J(same));

// ---- refusals and the free clock
var refused = false;
try { world.shaderTime({ t: -1 }); } catch (e) { refused = true; }
assert(refused, "a negative t is refused");
var free = world.shaderTime({ t: null });
assert(free.pinned === false, "{t: null} hands the clock back to the free clock: " + J(free));
var t0 = free.t;
editor.frame(12);
var later = world.shaderTime();
assert(later.pinned === false && later.t > t0, "the free clock counts frames forward: " + J(later));

assert(project.close(), "project.close");
console.log("shader_time e2e: done");
