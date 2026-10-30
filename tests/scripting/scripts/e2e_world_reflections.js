// editor.world_reflections — REFLECT-MODE-1 (PHOTON-7 stage 6; PHOTON-7-9A: the reflection has no mode):
// world.reflections({resolution}) is a scene property with one undo step, its names trimmed and case-insensitive, an
// unknown key or name refused - the deleted `mode` key among them - and the World panel's row (worldmodes:
// reflectionResolution) reads the same field; the deleted reflectionSources row is gone from the registry.
function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function J(x) { return JSON.stringify(x); }

var guid = project.create("World Reflections Test " + Date.now());
assert(guid.length > 10, "project created");
var r0 = world.reflections();
assert(r0.resolution === "auto" && r0.mode === undefined, "a new scene reads the default auto, and no mode (" + J(r0) + ")");
var pushes0 = editor.undoState().pushes;
var r1 = world.reflections({ resolution: " Half " });
assert(r1.resolution === "half", "names are trimmed and case-insensitive (" + J(r1) + ")");
assert(editor.undoState().pushes === pushes0 + 1, "the edit is ONE undo step");
var r2 = world.reflections();
assert(r2.resolution === "half", "the read returns what was set (" + J(r2) + ")");
var refusedKey = false, refusedName = false, refusedMode = false;
try { world.reflections({ quality: "high" }); } catch (e) { refusedKey = true; }
try { world.reflections({ resolution: "quarter" }); } catch (e) { refusedName = true; }
try { world.reflections({ mode: "both" }); } catch (e) { refusedMode = true; }
assert(refusedKey && refusedName && refusedMode, "an unknown key, an unknown name and the deleted mode are refused");
assert(world.reflections().resolution === "half", "a refused call changed nothing");
var ids = world.modeTable().rows.map(function (x) { return x.id; });
assert(ids.indexOf("reflectionResolution") >= 0, "the World panel's row is in the registry (world.modeTable)");
assert(ids.indexOf("reflectionSources") < 0, "the deleted sources row is not");
world.reflections({ resolution: "auto" });
assert(world.reflections().resolution === "auto", "set back to the default");
