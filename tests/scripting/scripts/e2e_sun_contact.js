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

// ---- 6. THE WORLD PANEL'S ROWS (SMALL-FIXES-3) ---------------------------------
// The Shadows section's Sun Contact rows, DRIVEN the way a person drives them
// (editor.propertyRow: a click, a pick, a typed value + Return) and read back
// through the verb: the row and world.sunContact are one model, each gesture
// is one undo step, and a scene that cannot trace greys what cannot act.
// A fresh open binds the panel to a known document (the rows refill on the
// panel's own triggers, like the Shadow Quality combo — not on a script edit).
world.sunContact({ enabled: false, range: 2, resolution: "auto" });
project.save();
project.close();
project.open(guid);
app.space("editor");
editor.select(scene.root());
editor.propertiesTab({ tab: "world" });
editor.frame(2);
function row(key, value) {
    var args = { tab: "world", key: key };
    if (value !== undefined) args.value = value;
    return editor.propertyRow(args);
}
var keys = editor.properties({ tab: "world" }).filter(function (r) {
    return r.key.indexOf("sunContact.") === 0;
});
assert(keys.length === 4, "the Shadows section carries the four Sun Contact rows (" +
       keys.map(function (r) { return r.key + "=" + r.label; }).join(", ") + ")");
assert(keys.every(function (r) {
    return r.section.indexOf("Shadows") >= 0 && ["contact", "sun", "rays"].every(function (w) {
        return r.keywords.indexOf(w) >= 0;
    });
}), "...in the Shadows section, findable by contact / sun / rays");
editor.propertiesFilter({ tab: "world", text: "contact" });
assert(editor.properties({ tab: "world" }).filter(function (r) {
    return r.key.indexOf("sunContact.") === 0 && r.filteredOut;
}).length === 0, "the filter box's 'contact' keeps every Sun Contact row");
editor.propertiesFilter({ tab: "world", text: "" });

var sw = row("sunContact.enabled");
assert(sw.control === "check" && sw.value === false, "the switch reads OFF, as the document holds");
assert(sw.label === "Sun Contact" && sw.toolTip.indexOf("contact shadows") >= 0,
       "...labelled Sun Contact, with a tooltip that says what it is");
var rg = row("sunContact.range");
assert(rg.control === "number" && Math.abs(rg.value - 2) < 1e-6, "the range reads 2 m");
assert(Math.abs(rg.min - 0.05) < 1e-6 && rg.max === 50, "...over the verb's band 0.05..50 m");
var rs = row("sunContact.resolution");
assert(rs.control === "combo" && rs.value === 0 && rs.items.length === 3,
       "the resolution reads Auto, of three (" + rs.items.join(" | ") + ")");

if (machineHasRays) {
    assert(sw.enabled && rg.enabled && rs.enabled, "rays here: every row is live");
    var p0 = pushes();
    row("sunContact.enabled", true);
    assert(world.sunContact().enabled === true, "CLICKING the switch turns the verb's row ON");
    assert(pushes() === p0 + 1, "...as exactly ONE undo step");
    editor.frame(4);
    p0 = pushes();
    rg = row("sunContact.range", 3.25);
    assert(Math.abs(world.sunContact().range - 3.25) < 1e-6 && Math.abs(rg.value - 3.25) < 1e-6,
           "a TYPED range (3.25 + Return) reads back through the verb");
    assert(pushes() === p0 + 1, "...as exactly ONE undo step");
    throws(function () { row("sunContact.range", 60); }, "a range past the band is refused at the row");
    assert(Math.abs(world.sunContact().range - 3.25) < 1e-6, "...and writes nothing");
    p0 = pushes();
    row("sunContact.resolution", 2);
    assert(world.sunContact().resolution === "half", "picking Half reads back 'half'");
    row("sunContact.resolution", "Full (one ray per pixel)");
    assert(world.sunContact().resolution === "full", "picking Full reads back 'full'");
    assert(pushes() === p0 + 2, "...one undo step per pick");
    var st = row("sunContact.status");
    console.log("   status row: " + st.value);
    assert(st.panelVisible && st.value.indexOf("Tracing") === 0,
           "the status row says what the renderer is doing: " + st.value);
    // (No undo here: a script's run is one open macro, so editor.undo() inside
    // it reaches the step BEFORE the script — the undo half of every gesture
    // is ScenePropertyCommand's, refreshed through the same rows.)
    row("sunContact.resolution", 0);
    row("sunContact.range", 2);
    row("sunContact.enabled", false);
    sc = world.sunContact();
    assert(sc.enabled === false && Math.abs(sc.range - 2) < 1e-6 && sc.resolution === "auto",
           "the rows walk the verb's row back to the start");
    st = row("sunContact.status");
    assert(!st.panelVisible, "...and the status row, with nothing to report, is hidden");

    // NO RAYS FOR THIS SCENE: what cannot act is greyed, and says why.
    var mode = world.rayTracing();
    world.rayTracing("off");
    row("sunContact.enabled", true);          // the gesture refreshes the section
    sw = row("sunContact.enabled");
    rg = row("sunContact.range");
    assert(sw.value === true && sw.enabled && !rg.enabled,
           "rays off with the row ON: the switch stays live (it can always be turned off), the range greys");
    row("sunContact.enabled", false);
    sw = row("sunContact.enabled");
    rg = row("sunContact.range");
    rs = row("sunContact.resolution");
    st = row("sunContact.status");
    assert(!sw.enabled && !rg.enabled && !rs.enabled,
           "rays off for the scene: the switch (off), range and resolution are GREYED");
    assert(st.panelVisible && st.value.indexOf("Needs rays") === 0, "...and the row says why: " + st.value);
    throws(function () { row("sunContact.range", 1); }, "a greyed row takes no gesture");
    world.rayTracing(mode);
    world.sunContact({ enabled: false, range: 2, resolution: "auto" });
} else {
    assert(!rg.enabled && !rs.enabled, "no rays here: range and resolution are greyed");
    throws(function () { row("sunContact.range", 1); }, "...and take no gesture");
}

console.log("PASS sun_contact");
