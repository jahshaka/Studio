// editor.world_reflections — REFLECT-MODE-1 (PHOTON-7 stage 6): world.reflections({mode, resolution}) is a scene
// property with one undo step, its names trimmed and case-insensitive, an unknown key or name refused, and the World
// panel's rows (worldmodes: reflectionSources, reflectionResolution) read the same fields.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var guid = project.create("World Reflections Test " + Date.now());
assert(guid.length > 10, "project created");
var r0 = world.reflections();
assert(r0.mode === "both" && r0.resolution === "auto", "a new scene reads the defaults both / auto (" + J(r0) + ")");
var pushes0 = editor.undoState().pushes;
var r1 = world.reflections({ mode: " RAYS ", resolution: "Half" });
assert(r1.mode === "rays" && r1.resolution === "half", "names are trimmed and case-insensitive (" + J(r1) + ")");
assert(editor.undoState().pushes === pushes0 + 1, "the edit is ONE undo step");
var r2 = world.reflections();
assert(r2.mode === "rays" && r2.resolution === "half", "the read returns what was set (" + J(r2) + ")");
var refusedKey = false, refusedName = false;
try { world.reflections({ quality: "high" }); } catch (e) { refusedKey = true; }
try { world.reflections({ mode: "mirror" }); } catch (e) { refusedName = true; }
assert(refusedKey && refusedName, "an unknown key and an unknown name are refused");
assert(world.reflections().mode === "rays", "a refused call changed nothing");
var ids = world.modeTable().rows.map(function (x) { return x.id; });
assert(ids.indexOf("reflectionSources") >= 0 && ids.indexOf("reflectionResolution") >= 0,
       "the World panel's rows are in the registry (world.modeTable)");
world.reflections({ mode: "both", resolution: "auto" });
assert(world.reflections().mode === "both", "set back to the defaults");
