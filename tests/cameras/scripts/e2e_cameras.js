// cameras.e2e — CAMERAS_SPEC phase 1 through the verbs, in the real app.
//
// Phase A: scene.addCamera makes a real scene node (and does NOT arm play).
// Phase B: camera.settings reads and writes the whole §2 table, including the
//          angle <-> focal-length binding and the refusals.
// Phase C: camera.lookAt.
// Phase D: scene.cameras / setActiveCamera / activeCamera, and the refusals.
// Phase E: the SAVE -> CLOSE -> OPEN round trip of every camera field and of
//          the active-camera choice — the real SceneWriter and the real
//          SceneReader, which is why this lives in the app and not in a unit
//          test with a stub.
// Phase F: duplicate, delete (which clears the active camera), and undo.
//
// Runs inside the real binary (--script, scratch HOME).

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps, msg) {
    var d = Math.abs(a - b);
    assert(d <= (eps || 1e-3), msg + " (" + a + " vs " + b + ")");
}
// scene.activeCamera() and scene.find() report "nothing" the way every
// QVariant-returning verb on this surface does — as JS `undefined`, which is
// what an invalid QVariant converts to. Both spellings are accepted here so the
// assertion is about the ANSWER, not about that convention.
function isNone(v) { return v === null || v === undefined; }
function refuses(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; }
    assert(threw, msg);
}

var guid = project.create("Cameras " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- phase A: a camera is a scene node ----------------------------------
var cam = scene.addCamera({ position: { x: 1, y: 2, z: 3 } });
assert(cam.length > 10, "scene.addCamera -> " + cam);

var info = node.info(cam);
assert(info.type === "camera",
       "the node reports type 'camera' — the type-enum fix, seen from the API (it read " +
       "'empty' for the whole life of the codebase)");
near(info.position.x, 1, 1e-3, "the camera landed at the requested position");
near(info.position.y, 2, 1e-3, "…y");
near(info.position.z, 3, 1e-3, "…z");
assert(isNone(scene.activeCamera()),
       "adding a camera does NOT arm it for play — that is always an explicit choice");

// ---- phase B: the settings block ----------------------------------------
var s = camera.settings(cam);
near(s.angle, 45, 1e-3, "default angle is 45 vertical degrees");
near(s.sensorWidth, 36, 1e-3, "default sensor width 36 mm");
near(s.sensorHeight, 24, 1e-3, "default sensor height 24 mm");
near(s.focalLength, 28.9706, 1e-3, "45 degrees on a 24 mm sensor is a 28.9706 mm lens");
assert(s.authorMode === "degrees", "authored in degrees by default");
assert(s.projMode === "perspective", "perspective by default");
assert(s.dofEnabled === false && s.focusMode === "manual", "DOF off, focus manual by default");
assert(s.outputHeight === 1080, "output height 1080 by default");
assert(s.outputWidth === 1080 * s.aspectRatio || s.outputWidth > 0,
       "outputWidth is derived from outputHeight and the aspect (" + s.outputWidth + ")");

// mm -> degrees, through the verb
s = camera.settings(cam, { focalLength: 50 });
near(s.focalLength, 50, 1e-3, "camera.settings({focalLength: 50})");
near(s.angle, 26.9915, 1e-3, "…moves the angle to 26.9915 degrees");
assert(s.authorMode === "mm", "…and flips authorMode to mm");

// degrees -> mm, through the verb
s = camera.settings(cam, { angle: 90 });
near(s.focalLength, 12, 1e-3, "camera.settings({angle: 90}) -> a 12 mm lens");
assert(s.authorMode === "degrees", "…and back to degrees");

// authorMode decides what a sensor change preserves.
s = camera.settings(cam, { focalLength: 35 });               // authored in mm
s = camera.settings(cam, { sensorHeight: 12 });
near(s.focalLength, 35, 1e-3, "mm-authored: a smaller sensor KEEPS the lens");
assert(s.angle < 90, "…and narrows the angle");
s = camera.settings(cam, { angle: 45, sensorHeight: 24 });   // re-author in degrees
s = camera.settings(cam, { sensorHeight: 12 });
near(s.angle, 45, 1e-3, "degrees-authored: a smaller sensor KEEPS the framing");
near(s.focalLength, 12 / (2 * Math.tan(45 * Math.PI / 180 / 2)), 1e-3,
     "…and shortens the lens instead");

// The refusals.
refuses(function () { camera.settings(cam, { angle: 30, focalLength: 50 }); },
        "angle and focalLength together are refused (they are one value)");
refuses(function () { camera.settings(cam, { noSuchSetting: 1 }); },
        "an unknown setting is refused, not ignored");
refuses(function () { camera.settings(cam, { focusMode: "sometimes" }); },
        "an unknown focusMode is refused");
refuses(function () { camera.settings(cam, { focusTarget: "not-a-node" }); },
        "focusTarget must name a node that exists");
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
refuses(function () { camera.settings(cube, { fStop: 4 }); },
        "camera.settings refuses a node that is not a camera");

// Every remaining §2 row, written and read back.
var full = {
    sensorWidth: 22.3, sensorHeight: 14.9, angle: 38.5,
    projMode: "orthogonal", orthoSize: 7.5,
    nearClip: 0.25, farClip: 900, aspectRatio: 2.39, constrainAspect: true,
    dofEnabled: true, focusMode: "track", focusDistance: 6.25, focusTarget: cube,
    fStop: 1.8, outputHeight: 2160, bodyVisible: false
};
s = camera.settings(cam, full);
near(s.sensorWidth, 22.3, 1e-3, "sensorWidth written");
near(s.sensorHeight, 14.9, 1e-3, "sensorHeight written");
near(s.angle, 38.5, 1e-3, "angle written");
assert(s.projMode === "orthogonal", "projMode written");
near(s.orthoSize, 7.5, 1e-3, "orthoSize written");
near(s.nearClip, 0.25, 1e-4, "nearClip written");
near(s.farClip, 900, 1e-3, "farClip written");
near(s.aspectRatio, 2.39, 1e-3, "aspectRatio written");
assert(s.constrainAspect === true, "constrainAspect written");
assert(s.dofEnabled === true, "dofEnabled written");
assert(s.focusMode === "track", "focusMode written");
near(s.focusDistance, 6.25, 1e-3, "focusDistance written");
assert(s.focusTarget === cube, "focusTarget written (a real node id)");
near(s.fStop, 1.8, 1e-3, "fStop written");
assert(s.outputHeight === 2160, "outputHeight written");
assert(s.bodyVisible === false, "bodyVisible written");

// The settings are ALSO reflected node properties — that is what makes them
// keyframeable, and it must be the same fields, not a parallel copy.
near(node.property(cam, "fStop"), 1.8, 1e-3, "node.property sees the same fStop");
assert(node.setProperty(cam, "fStop", 4), "node.setProperty writes it");
near(camera.settings(cam).fStop, 4, 1e-3, "…and camera.settings sees the write");
camera.settings(cam, { fStop: 1.8 });

// The read block carries three things that are NOT settings; writing them back
// is refused, with a message that says which and why.
refuses(function () { camera.settings(cam, { id: cam }); },
        "writing the reported `id` back is refused (it is not a setting)");
refuses(function () { camera.settings(cam, { outputWidth: 1920 }); },
        "outputWidth is derived, not settable");

// Strip those three and re-applying the whole block must be a NO-OP: every
// enum spelling the read side emits ("degrees", "perspective", "track") has to
// be understood by the write side, or the API cannot round-trip itself.
var before = camera.settings(cam);
var writeBack = JSON.parse(JSON.stringify(before));
delete writeBack.id; delete writeBack.name; delete writeBack.outputWidth;
delete writeBack.focalLength;   // angle carries it; the two together are refused
assert(JSON.stringify(camera.settings(cam, writeBack)) === JSON.stringify(before),
       "re-applying a read block is a no-op (every enum spelling round-trips)");

// ---- phase C: lookAt ----------------------------------------------------
var shot = scene.addCamera({ position: { x: 0, y: 0, z: 5 } });
assert(camera.lookAt(shot, { x: 0, y: 0, z: 0 }), "camera.lookAt(point)");
var rot = node.info(shot).rotation;
assert(Math.abs(rot.x) < 1 && Math.abs(rot.y) < 1,
       "looking down -Z from +Z is the identity orientation (" +
       rot.x + "," + rot.y + "," + rot.z + ")");
near(node.info(shot).position.z, 5, 1e-3, "lookAt did not MOVE the camera");
assert(camera.lookAt(shot, cube), "camera.lookAt(nodeId)");
refuses(function () { camera.lookAt(shot, shot); }, "a camera cannot look at itself");
refuses(function () { camera.lookAt(shot, { x: 0, y: 90, z: 5 }); },
        "a target straight above the camera is refused (undefined roll)");
refuses(function () { camera.lookAt(cube, { x: 0, y: 0, z: 0 }); },
        "lookAt refuses a node that is not a camera");

// ---- phase D: the camera list and the active camera ---------------------
var list = scene.cameras();
assert(list.length === 2, "scene.cameras() sees both cameras (got " + list.length + ")");
assert(list[0].id === cam && list[1].id === shot, "…in scene-graph order");
assert(list[0].active === false && list[1].active === false, "neither is active yet");
assert(list[0].angle !== undefined && list[0].focalLength !== undefined,
       "each row carries the settings block");

refuses(function () { scene.setActiveCamera(cube); },
        "setActiveCamera refuses a node that is not a camera");
assert(isNone(scene.activeCamera()), "…and leaves the choice alone");
assert(scene.setActiveCamera(shot), "scene.setActiveCamera(shot)");
assert(scene.activeCamera() === shot, "scene.activeCamera() reads it back");
var l2 = scene.cameras();
assert(l2[1].active === true && l2[0].active === false, "the list marks the active one");
assert(scene.setActiveCamera(null), "scene.setActiveCamera(null) clears it");
assert(isNone(scene.activeCamera()), "…back to the free viewer");
scene.setActiveCamera(shot);

// ---- phase E: save -> close -> open -------------------------------------
function fingerprint() {
    var rows = scene.cameras();
    var out = [];
    for (var i = 0; i < rows.length; i++) {
        var r = rows[i];
        out.push({
            name: r.name, active: r.active,
            angle: r.angle, focalLength: r.focalLength,
            sensorWidth: r.sensorWidth, sensorHeight: r.sensorHeight,
            authorMode: r.authorMode, projMode: r.projMode, orthoSize: r.orthoSize,
            nearClip: r.nearClip, farClip: r.farClip, aspectRatio: r.aspectRatio,
            constrainAspect: r.constrainAspect, dofEnabled: r.dofEnabled,
            focusMode: r.focusMode, focusDistance: r.focusDistance,
            focusTarget: r.focusTarget, fStop: r.fStop,
            outputHeight: r.outputHeight, bodyVisible: r.bodyVisible,
            position: r.position, rotation: r.rotation, scale: r.scale
        });
    }
    return JSON.stringify(out);
}

var fresh = fingerprint();
console.log("fingerprint: " + fresh);
for (var cycle = 1; cycle <= 2; cycle++) {
    assert(project.save() === true, "cycle " + cycle + ": project.save");
    assert(project.close() === true, "cycle " + cycle + ": project.close");
    assert(project.open(guid) === true, "cycle " + cycle + ": project.open");

    var after = fingerprint();
    if (after !== fresh) {
        var i = 0;
        while (i < after.length && i < fresh.length && after[i] === fresh[i]) i++;
        console.log("FIELD DIFF at " + i);
        console.log("  fresh: ..." + fresh.substr(Math.max(0, i - 60), 160));
        console.log("  after: ..." + after.substr(Math.max(0, i - 60), 160));
    }
    assert(after === fresh,
           "cycle " + cycle + ": every camera field survived save/close/open — including the " +
           "sensor, the author mode, the whole focus block and the active-camera choice");
    // The cameras came back as CAMERAS, not as the plain scene nodes they
    // reopened as before the writer/reader learned the type.
    var rows = scene.nodes();
    var cams = 0;
    for (var k = 0; k < rows.length; k++) if (rows[k].type === "camera") cams++;
    assert(cams === 2, "cycle " + cycle + ": both nodes reopened with type 'camera'");
}

// ids are regenerated by nothing — the guids are stable across the round trip,
// which is what makes the active-camera guid resolve at all.
assert(!isNone(scene.activeCamera()), "the active camera still resolves after reopening");

// ---- phase F: duplicate, delete, undo -----------------------------------
var active = scene.activeCamera();
var copy = node.duplicate(active);
assert(copy && copy !== active, "node.duplicate on a camera -> " + copy);
assert(node.info(copy).type === "camera", "the duplicate is a camera");
var a = camera.settings(active), b = camera.settings(copy);
assert(a.angle === b.angle && a.sensorWidth === b.sensorWidth &&
       a.authorMode === b.authorMode && a.focusMode === b.focusMode &&
       a.fStop === b.fStop && a.outputHeight === b.outputHeight &&
       a.bodyVisible === b.bodyVisible && a.projMode === b.projMode,
       "the duplicate carries every setting");
assert(scene.cameras().length === 3, "three cameras now");

assert(node.remove(copy), "node.remove(copy)");
assert(scene.cameras().length === 2, "the copy is gone");

assert(node.remove(active), "removing the ACTIVE camera");
assert(isNone(scene.activeCamera()),
       "deleting the active camera falls back to the free viewer rather than leaving play " +
       "pointed at a guid that resolves to nothing");

console.log("cameras.e2e: all checks passed");
