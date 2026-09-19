// scripting.e2e.primitives — THE ONE PRIMITIVE TABLE (owner review R6, answer
// Q4: "PRIMITIVES ONLY"; lane SMALL-UI-A).
//
// There were four copies of this list plus a fifth partial one in the Add menu,
// and they had already drifted (Cone, Capsule and Pyramid could not be reached
// from the menu at all). Everything below reads the SAME table
// (src/data/primitives.h), so this suite is what stops it splitting again:
//
//   1. EVERY ROW ADDS. `assets.builtins()` lists the primitives the drawer's
//      tiles and the drop payload use; every one of those names adds a node
//      with real geometry — vertices, triangles, and TANGENTS, without which
//      HlmsPbs throws on a normal-mapped material and the object falls back to
//      flat grey (CLAUDE.md, the 2026-08-29 facts).
//   2. EVERY GUID IS UNIQUE, and none of them collides with the reserved
//      MATERIAL guids any more — the old primitive numbers sat on top of
//      BuiltinShaders' 0002-0006 and were safe only because every lookup
//      happened to be type-scoped.
//   3. AN OLD GUID STILL RESOLVES. A favourite saved before the renumber names
//      a guid that is no longer in the table; primitives::canonicalGuid maps it
//      in one place, which is what the drop path asks.
//   4. GEAR, SPONGE, STEPS AND THE TEAPOT ARE GONE, and asking for one says so
//      by name instead of doing nothing (which is what the old fall-through
//      loop did for any name it did not recognise).
//   5. THE TEAPOT'S MESH IS STILL THERE, because four shipped samples name that
//      exact resource path in their stored scene blobs.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var proj = project.create("Primitives " + Date.now());
assert(proj.length > 10, "project.create");

// ---- the table, as the drawer and the drop see it ---------------------------
var builtins = assets.builtins();
var prims = [];
for (var i = 0; i < builtins.length; ++i)
    if (builtins[i].kind === "primitive") prims.push(builtins[i]);
console.log("primitives: " + J(prims));
assert(prims.length >= 12, "the table has at least the twelve tiles (got " + prims.length + ")");

// ---- 1. every row adds, with real geometry ---------------------------------
// (The vertex layout itself — normals, UVs and TANGENTS on every row, plus the
// tile icon files and the old-guid mapping — is defaults.primitive_table, which
// reads the table and the resources directly. This half is the VERB: that every
// name the drawer offers is a name the verb accepts, and that what comes back
// is a mesh node with a box.)
var seen = {};
for (var i = 0; i < prims.length; ++i) {
    var name = prims[i].name;
    var id = scene.addPrimitive(name);
    assert(id && String(id).length > 0, "scene.addPrimitive('" + name + "')");
    var rows = scene.nodes({ subtree: id, depth: 0 });
    assert(rows.length === 1 && rows[0].type === "mesh", "...'" + name + "' is a mesh node");
    seen[name] = id;
}
// Ground has no tile, so it is not in `prims` — but it IS a name the verb takes,
// and it was missing from one of the four old lists entirely.
var ground = scene.addPrimitive("Ground");
assert(ground && String(ground).length > 0, "scene.addPrimitive('Ground') — the row with no tile");
seen["Ground"] = ground;

for (var name in seen) {
    var b = scene.bounds({ nodes: [seen[name]] });
    console.log("  " + name + " size " + J([b.size.x.toFixed(3), b.size.y.toFixed(3),
                                            b.size.z.toFixed(3)]));
    // TWO axes, not three: Plane and Ground are flat on purpose, and a mesh
    // that failed to parse has NO extent at all.
    var axes = [b.size.x, b.size.y, b.size.z].filter(function (v) { return v > 0.01; });
    assert(axes.length >= 2,
           name + " loaded real geometry (a mesh that failed to parse has an empty box)");
}

// ---- 2. the guids ------------------------------------------------------------
var guids = {};
for (var i = 0; i < prims.length; ++i) {
    var g = prims[i].guid;
    assert(!guids[g], "guid " + g + " is unique (" + prims[i].name + ")");
    guids[g] = prims[i].name;
}
for (var i = 0; i < builtins.length; ++i) {
    if (builtins[i].kind === "primitive") continue;
    assert(!guids[builtins[i].guid],
           "no primitive guid collides with the reserved material guid " + builtins[i].guid
           + " (" + builtins[i].name + ")");
}

// ---- 4. the retired names ----------------------------------------------------
var retired = ["Gear", "Sponge", "Steps", "Teapot"];
for (var i = 0; i < retired.length; ++i) {
    var threw = null;
    try { scene.addPrimitive(retired[i]); } catch (e) { threw = String(e); }
    assert(threw !== null, retired[i] + " is refused");
    console.log("   -> " + threw);
    assert(threw.indexOf(retired[i]) >= 0, "...naming it");
    assert(threw.indexOf("no longer a built-in primitive") >= 0,
           "...with a helpful message, not the generic unknown-primitive list");
}
for (var i = 0; i < builtins.length; ++i)
    assert(retired.indexOf(builtins[i].name) < 0,
           builtins[i].name + " is not listed as a builtin any more");

// ...and a name that never existed still gets the ordinary answer.
var typo = null;
try { scene.addPrimitive("Dodecahedron"); } catch (e) { typo = String(e); }
assert(typo !== null && typo.indexOf("unknown primitive") >= 0,
       "an unknown name gets the list, not the retirement notice");

// ---- 5. the teapot MESH is still shipped ------------------------------------
// Four samples name `:/content/primitives/teapot.obj` in their scene blobs; the
// honest read is one of those samples, opened, still standing on its teapot.
assert(project.openSample("Mirror Room") === true,
       "project.openSample('Mirror Room') (" + app.lastError() + ")");
var turns = 0;
while (project.archiveState() === "running") { editor.frame(1); if (++turns > 40000) break; }
assert(project.archiveState() === "idle", "the sample's import finished");
while (project.openState() === "opening") { editor.frame(1); if (++turns > 40000) break; }
assert(project.openState() === "idle", "the open finished (" + turns + " frames)");

var pot = scene.find("GreenTeapot");
assert(!!pot, "the Mirror Room still opens with its teapot (the mesh is still shipped)");
var pb = scene.bounds({ nodes: [pot] });
console.log("teapot size " + J([pb.size.x.toFixed(3), pb.size.y.toFixed(3), pb.size.z.toFixed(3)]));
assert(pb.size.x > 0.5 && pb.size.y > 0.5,
       "...and its mesh actually loaded — the sample opens exactly as it did before");

console.log("PASS scripting.e2e.primitives");
