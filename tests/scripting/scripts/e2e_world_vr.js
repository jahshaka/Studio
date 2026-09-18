// scripting.e2e.world_vr — THE PROJECT'S VR SETTINGS (lane VR-WORLD-1; the
// owner's request 2026-09-18: "default fly speed — use what you like — and add
// a VR World settings section where we can set that").
//
// HOW A WEARER MOVES IS A PROPERTY OF THE PROJECT. A world authored at
// architectural scale is walked at a different pace from a tabletop one, and
// the author who chose that pace expects it back the next time they put the
// headset on — which is why these are document fields (saved with the scene,
// travelling with the asset) and not an application preference, exactly like
// world.rayTracing.
//
// WHAT IS ASSERTED HERE: the verb's read, its writes, its clamps, its refusals,
// its undo shape, the save/close/open round trip through the real writer and
// the real reader, and THE SESSION RULE — `vr.locomotion` overrides these for
// one session and writes nothing to the document, while `vr.locomotion()` and
// `vr.interactionMode()` report the EFFECTIVE values.
//
// WHAT IS NOT HERE: a wearer actually moving. Engine::setVrOrigin is a no-op
// without a session, so a rig only exists where a runtime does — the project's
// fly speed walking a wearer the measured distance is `vr.input_session`, on
// Monado's simulated headset, and the World panel's own section is
// `ui.vr_panel`.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, needle, msg) {
    try { fn(); } catch (e) {
        assert(String(e).indexOf(needle) >= 0, msg + " (" + e + ")");
        return;
    }
    throw new Error("assert failed: " + msg + " — it did not throw");
}
function near(a, b) { return Math.abs(a - b) <= 1e-6; }
function pushes() { return editor.undoState().pushes; }

// ---- the registry ---------------------------------------------------------
var worldModule = api.verbs().filter(function (m) { return m.module === "world"; })[0];
var worldNames = worldModule.verbs.map(function (v) { return v.name; });
assert(worldNames.indexOf("vr") >= 0, "world.vr is registered");
assert(worldNames.indexOf("setVr") >= 0, "...beside the set* alias every noun-write verb has");

var guid = project.create("VR world " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- 1. the defaults ------------------------------------------------------
var d = world.vr();
console.log("   world.vr() = " + JSON.stringify(d));
assert(near(d.flySpeed, 15), "a new project flies at 15 m/s (the lead's pick, 2026-09-18)");
assert(d.fly === "aim", "...along the stick hand's own aim ray");
assert(d.turn === "snap", "...turning by snap");
assert(near(d.snapTurnDegrees, 30), "...30 degrees a flick");
assert(near(d.smoothTurnDegreesPerSecond, 90), "...90 degrees a second when it is smooth");
assert(d.dominant === "right", "...with the right hand manipulating");

// ---- 2. every key writes and reads back ------------------------------------
var w = world.vr({ flySpeed: 3, fly: "level", turn: "smooth", snapTurnDegrees: 45,
                   smoothTurnDegreesPerSecond: 120, dominant: "left" });
assert(near(w.flySpeed, 3) && w.fly === "level" && w.turn === "smooth" &&
       near(w.snapTurnDegrees, 45) && near(w.smoothTurnDegreesPerSecond, 120) &&
       w.dominant === "left", "every key written in one call: " + JSON.stringify(w));
assert(JSON.stringify(world.vr()) === JSON.stringify(w), "...and the read agrees");
// The enums are trimmed and case-insensitive, like every other named state.
assert(world.vr({ fly: "  GAZE " }).fly === "gaze", "a mode name is trimmed and folded");
assert(world.setVr({ fly: "aim" }).fly === "aim", "the setVr alias is the same verb");

// ---- 3. what is refused, and what is clamped -------------------------------
// A NUMBER THAT IS NOT A SETTING is refused by name: zero is a wearer who
// cannot move and a negative one walks backwards for ever — both are typos.
throws(function () { world.vr({ flySpeed: 0 }); }, "flySpeed",
       "a fly speed of zero is refused");
throws(function () { world.vr({ flySpeed: -4 }); }, "flySpeed",
       "...and a negative one");
throws(function () { world.vr({ flySpeed: "quick" }); }, "flySpeed",
       "...and a word where a number belongs");
throws(function () { world.vr({ snapTurnDegrees: 0 }); }, "snapTurnDegrees",
       "a snap step of zero is refused");
// A BOOLEAN IS NOT A NUMBER, and this one bites because QVariant converts it
// happily: `flySpeed: true` used to set a wearer walking at 1 m/s (the Fable
// read of VR-WORLD-1, item 4).
throws(function () { world.vr({ flySpeed: true }); }, "flySpeed",
       "true where a number belongs is refused, not read as 1");
throws(function () { world.vr({ snapTurnDegrees: false }); }, "snapTurnDegrees",
       "...and false is not zero either");
throws(function () { world.vr({ turn: "spinny" }); }, "spinny",
       "an unknown turn mode is refused, never guessed");
throws(function () { world.vr({ fly: "backwards" }); }, "backwards",
       "...and an unknown fly mode");
throws(function () { world.vr({ dominant: "either" }); }, "either",
       "...and an unknown hand");
throws(function () { world.vr({ speed: 4 }); }, "unknown key",
       "an unknown KEY is refused with the list");
var afterRefusals = world.vr();
assert(afterRefusals.fly === "aim" && near(afterRefusals.flySpeed, 3),
       "every refusal left the project exactly as it was");
// ...AND A REFUSAL IS WHOLE: one bad value writes none of the good ones.
throws(function () { world.vr({ flySpeed: 9, turn: "sideways" }); }, "sideways",
       "a call with one bad value is refused");
assert(near(world.vr().flySpeed, 3), "...and its GOOD value was not written either");
// MERELY OUT OF RANGE IS CLAMPED — these are continuous dials, and the panel's
// own drag row cannot leave the range either.
assert(near(world.vr({ flySpeed: 1000 }).flySpeed, 100), "a fly speed above the dial clamps");
assert(near(world.vr({ snapTurnDegrees: 400 }).snapTurnDegrees, 180), "...and the snap step");
assert(near(world.vr({ smoothTurnDegreesPerSecond: 5000 }).smoothTurnDegreesPerSecond, 720),
       "...and the smooth rate");

// ---- 4. one call, one undo step -------------------------------------------
world.vr({ flySpeed: 15, fly: "aim", turn: "snap", snapTurnDegrees: 30,
           smoothTurnDegreesPerSecond: 90, dominant: "right" });
var before = pushes();
world.vr({ flySpeed: 6 });
assert(pushes() === before + 1, "a change records exactly ONE undo step");
before = pushes();
world.vr({ flySpeed: 6 });
assert(pushes() === before, "setting the value it already has records NOTHING");
before = pushes();
world.vr({ flySpeed: 7, turn: "smooth", dominant: "left" });
assert(pushes() === before + 1, "THREE settings in one call are still ONE step");
before = pushes();
try { world.vr({ flySpeed: -1 }); } catch (e) {}
assert(pushes() === before, "and a refused call records nothing");

// ---- 5. saved with the scene (the writer and the reader) -------------------
world.vr({ flySpeed: 4.5, fly: "gaze", turn: "smooth", snapTurnDegrees: 15,
           smoothTurnDegreesPerSecond: 45, dominant: "left" });
var saved = world.vr();
project.save();
project.close();
project.open(guid);
var reopened = world.vr();
assert(JSON.stringify(reopened) === JSON.stringify(saved),
       "every VR setting survived save / close / open: " + JSON.stringify(reopened));
// ...and a project that never set them opens at the constructor's values, which
// is the reader-defaults law: "absent" and "fresh" must be the same scene.
var second = project.create("VR world defaults " + Date.now());
assert(near(world.vr().flySpeed, 15) && world.vr().fly === "aim",
       "a fresh project still reads the documented defaults");
project.save();
project.close();
project.open(second);
assert(near(world.vr().flySpeed, 15) && world.vr().turn === "snap",
       "...and so does it after a round trip that wrote them explicitly");

// ---- 6. THE SESSION RULE ---------------------------------------------------
// vr.locomotion reports the EFFECTIVE values — the project's, with this
// session's overrides — and an override writes nothing to the document.
project.open(guid);
var doc = world.vr();
var loco = vr.locomotion();
console.log("   vr.locomotion() = " + JSON.stringify(loco));
assert(near(loco.flySpeed, doc.flySpeed) && loco.fly === doc.fly && loco.turn === doc.turn &&
       near(loco.snapTurnDegrees, doc.snapTurnDegrees) &&
       near(loco.smoothTurnDegreesPerSecond, doc.smoothTurnDegreesPerSecond) &&
       loco.dominant === doc.dominant,
       "with nothing overridden the locomotion IS the project's settings");
assert(loco.overridden.length === 0, "...and nothing is listed as overridden");
assert(loco.session === false,
       "no session has latched a project: the live document is what is read");

var over = vr.locomotion({ flySpeed: 9, turn: "smooth" });
assert(near(over.flySpeed, 9) && over.turn === "smooth", "an override takes effect at once");
assert(over.overridden.indexOf("flySpeed") >= 0 && over.overridden.indexOf("turn") >= 0,
       "...and says which keys this session changed: " + JSON.stringify(over.overridden));
assert(over.fly === doc.fly && over.dominant === doc.dominant,
       "...while everything else is still the project's");
var docAfter = world.vr();
assert(JSON.stringify(docAfter) === JSON.stringify(doc),
       "AN OVERRIDE WRITES NOTHING TO THE DOCUMENT: " + JSON.stringify(docAfter));

// The interaction reports the same effective numbers — one model, not two.
var mode = vr.interactionMode();
assert(near(mode.flySpeed, 9) && mode.turn === "smooth" && mode.fly === doc.fly,
       "vr.interactionMode() reports the effective values: " + JSON.stringify(
           { flySpeed: mode.flySpeed, turn: mode.turn, fly: mode.fly }));
assert(mode.dominant === doc.dominant, "...including the hand the project chose");

// A DOCUMENT EDIT IS SEEN while no session is latched (there is nothing to
// protect a wearer from yet), and the override still wins over it.
world.vr({ fly: "level", flySpeed: 2 });
var afterEdit = vr.locomotion();
assert(afterEdit.fly === "level", "a project edit with no session running is read at once");
assert(near(afterEdit.flySpeed, 9), "...and the session's override still wins over it");

// vr.locomotion refuses exactly what world.vr refuses — one table, one set of
// rules, so the two verbs cannot disagree.
throws(function () { vr.locomotion({ flySpeed: 0 }); }, "flySpeed",
       "vr.locomotion refuses a zero fly speed too");
throws(function () { vr.locomotion({ turn: "spinny" }); }, "spinny",
       "...and an unknown turn mode");
throws(function () { vr.locomotion({ nonsense: 1 }); }, "unknown key",
       "...and an unknown key");
before = pushes();
vr.locomotion({ flySpeed: 12 });
assert(pushes() === before, "a session override is NOT a document edit: no undo step");

// ---- 7. THE OVERRIDES OUTLIVE NO SESSION, AND DIE WITH ONE ---------------
// (the Fable read, item 3.) An override asked for BEFORE a session starts is a
// caller asking for something; adopting a project used to throw it away
// silently. There is no session in this process, so what is asserted here is
// the half that holds everywhere: the override stands until something releases
// it. The session half — it survives `vr.begin` and is gone after `vr.end` — is
// `vr.input_session`, where sessions exist.
vr.locomotion({ flySpeed: 11 });
assert(near(vr.locomotion().flySpeed, 11), "an override with no session stands");
world.vr({ flySpeed: 5 });
assert(near(vr.locomotion().flySpeed, 11),
       "...and still wins over a project edit made after it");
assert(near(world.vr().flySpeed, 5), "while the document holds what world.vr wrote");

console.log("PASS world_vr");
