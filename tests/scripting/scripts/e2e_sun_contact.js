// scripting.e2e.sun_contact — HARD SUN CONTACT SHADOWS AS A PROJECT ROW
// (PHOTON P5, RY-R3; lane PHOTON-RAYS-1).
//
// The verb is the whole authoring surface (API-first): world.sunContact reads
// and writes the document's `sunContact` block — enabled, range, resolution —
// and reports what the renderer did with it (`live`). What the PICTURE does is
// gi.sun_contact's business (the contact gap closing, the far shadow unchanged,
// the no-rays picture exact); this suite is the row's contract: the default,
// the validation, one undo step per change, the file round trip, and the
// renderer's answer on this machine.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; console.log("ok: " + msg + " (" + e.message + ")"); }
    if (!threw) throw new Error("assert failed (no error): " + msg);
}

var worldModule = api.verbs().filter(function (m) { return m.module === "world"; })[0];
assert(worldModule.verbs.map(function (v) { return v.name; }).indexOf("sunContact") >= 0,
       "world.sunContact is registered");

var guid = project.create("Sun contact " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- 1. OFF BY DEFAULT ------------------------------------------------------
var sc = world.sunContact();
assert(sc.enabled === false, "a new project has the row OFF");
assert(sc.range === 2, "the default range is 2 m");
assert(sc.resolution === "auto", "the default resolution follows the tier");
assert(world.get().sunContact.enabled === false, "world.get() reports it too");

// ---- 2. set, read back, clamp, refuse ----------------------------------------
sc = world.sunContact({ enabled: true, range: 3.5, resolution: "half" });
assert(sc.enabled === true && sc.range === 3.5 && sc.resolution === "half", "set all three");
assert(world.sunContact().range === 3.5, "...and they read back");
assert(world.sunContact({ resolution: " FULL " }).resolution === "full",
       "the resolution is trimmed and case-insensitive");
throws(function () { world.sunContact({ range: 1000 }); }, "a range past the band (50 m) is REFUSED");
throws(function () { world.sunContact({ range: 0.001 }); }, "...and one below it (0.05 m)");
throws(function () { world.sunContact({ range: -1 }); }, "a negative range is REFUSED");
assert(world.sunContact({ range: 50 }).range === 50 && Math.abs(world.sunContact({ range: 0.05 }).range - 0.05) < 1e-6,
       "the band's two ends are accepted");
world.sunContact({ range: 3.5 });
throws(function () { world.sunContact({ range: "far" }); }, "a range that is not a number is refused");
throws(function () { world.sunContact({ resolution: "quarter" }); }, "an unknown resolution is refused");
throws(function () { world.sunContact({ enabeld: false }); }, "an unknown key is refused");
throws(function () { world.sunContact({ enabled: false, resolution: "nope" }); },
       "a call with one bad value...");
assert(world.sunContact().enabled === true, "...writes none of the good ones");

// ---- 3. one change, one undo step ---------------------------------------------
function pushes() { return editor.undoState().pushes; }
var before = pushes();
world.sunContact({ range: 2.5, resolution: "auto" });
assert(pushes() === before + 1, "a change of two fields records exactly ONE undo step");
before = pushes();
world.sunContact({ range: 2.5 });
assert(pushes() === before, "setting what it already holds records NOTHING");

// ---- 4. saved with the scene ---------------------------------------------------
world.sunContact({ enabled: true, range: 1.25, resolution: "full" });
project.save();
project.close();
project.open(guid);
sc = world.sunContact();
assert(sc.enabled === true && sc.range === 1.25 && sc.resolution === "full",
       "the row survived save / close / open");
world.sunContact({ enabled: false, range: 2, resolution: "auto" });
project.save();
project.close();
project.open(guid);
assert(world.sunContact().enabled === false, "...and so did turning it back off");

// ---- 5. what the renderer did -------------------------------------------------
editor.frame(2);                           // the device exists once something has rendered
var machineHasRays = world.giStatus().rayQuery.available === true;
console.log("   this machine: rayQuery.available = " + machineHasRays);
var live = world.sunContact().live;
assert(live && live.on === false && live.running === false, "the row off: nothing runs");
world.sunContact({ enabled: true, resolution: "full" });
editor.frame(4);
live = world.sunContact().live;
console.log("   live: " + JSON.stringify(live));
if (machineHasRays) {
    assert(live.on === true, "the row on resolves ON where the machine traces");
    assert(live.running === true, "...and the editor's view dispatched it (" + live.reason + ")");
    assert(live.divisor === 1 && live.width === live.targetWidth && live.height === live.targetHeight,
           "full resolution is one ray per pixel of the view");
    var t = live.toSun, len = Math.sqrt(t[0] * t[0] + t[1] * t[1] + t[2] * t[2]);
    assert(Math.abs(len - 1) < 1e-3 && t[1] > 0, "the rays go UP towards the sun (a unit vector)");
    world.sunContact({ resolution: "half" });
    editor.frame(4);
    live = world.sunContact().live;
    assert(live.divisor === 2 && live.width === Math.ceil(live.targetWidth / 2),
           "half resolution is one ray per 2x2 block");
} else {
    assert(live.on === false && live.running === false,
           "a machine without ray queries does not resolve the row on");
}
world.sunContact({ enabled: false });
editor.frame(2);
live = world.sunContact().live;
assert(live.on === false && live.running === false, "off again: nothing runs");

console.log("PASS sun_contact");
