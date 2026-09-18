// scripting.e2e.vr_teleport — THE THROWN ARC AND WHERE IT PUTS THE WEARER
// (SPECS/VR_INPUT_SPEC.md §6 row L4; the owner's answer 8, after the stage-1
// smoke).
//
// THE SAME INJECTION ROUTE every other VR gesture is gated on: this process was
// launched WITHOUT --vr, so no OpenXR loader was ever opened and no session
// exists, and the whole gesture nevertheless runs through the code a wearer
// drives — `vr.inject` writes the hand the runtime's action system would have
// written, `vr.step()` runs one interaction frame on it, and the arc is traced
// against the real document with the real picker.
//
// WHAT IS *NOT* HERE, and where it is instead:
//   * THE ARITHMETIC — the parabola, the plane solve, the slope rule and the
//     rig that arrives level and at the wearer's own height — is
//     `vr.grab_maths`, pure and display-free, with hand-computed expectations.
//   * THE WEARER ACTUALLY MOVING. It cannot happen here and that is a measured
//     engine fact rather than a gap: Engine::setVrOrigin is a no-op without a
//     session (OgreEngine.cpp), so there is no rig to move and the verb
//     REFUSES. What this asserts is the refusal, in words; the rig moving is
//     `vr.input_session` on Monado's simulated runtime.
//
// EVERY EXPECTED NUMBER BELOW IS THE THROW'S OWN ARITHMETIC, written out: a
// marker leaves the hand at 10 m/s along the aim and falls at 9.81 m/s², so a
// level throw from 1.4 m reaches the floor sqrt(2·1.4/9.81) = 0.5343 s later,
// 5.343 m along the aim. Nothing here is a tolerance around a mystery.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function near(a, b, eps) { return Math.abs(a - b) <= (eps === undefined ? 1e-3 : eps); }
function throws(fn, needle, msg) {
    try { fn(); } catch (e) {
        assert(String(e).indexOf(needle) >= 0, msg + " (" + e + ")");
        return;
    }
    throw new Error("assert failed: " + msg + " — it did not throw");
}
function show(tag, p) {
    console.log("      " + tag + " = (" + p.x.toFixed(3) + ", " + p.y.toFixed(3) + ", "
                + p.z.toFixed(3) + ")");
}

/// ONE FRAME OF THE AIMING HAND. Every field every time: `vr.inject` REPLACES
/// a hand's whole sample, so there is nothing to inherit and nothing to forget.
function aim(o) {
    var pose = { x: o.x, y: o.y === undefined ? 1.4 : o.y, z: o.z,
                 yaw: o.yaw || 0, pitch: o.pitch || 0 };
    var m = { valid: true, aim: pose, grip: pose,
              select: o.select || 0, grab: o.grab || 0, menuPressed: !!o.menu,
              stick: { x: o.stickX || 0, y: o.stickY || 0 },
              focused: o.focused === undefined ? true : !!o.focused };
    assert(vr.inject("right", m) === true && vr.step() === true,
           "one frame: the hand at (" + pose.x + ", " + pose.y + ", " + pose.z + ") pitch "
           + pose.pitch + (o.stickY ? " stick " + o.stickY : "") + (o.menu ? " menu" : "")
           + (o.grab ? " grab" : "") + (o.focused === false ? " UNFOCUSED" : ""));
}
function tp() { return vr.inputState().teleport; }

project.create("vr teleport " + Date.now());
// A frame so the engine has the scene, its bounds and its helper channel: the
// arc's line nodes are built on the viewport's own engine scene.
editor.frame(2);

// ---- 0. NOTHING IS AIMED YET --------------------------------------------

var t0 = tp();
console.log("teleport, cold: " + JSON.stringify(t0));
assert(t0.armed === false, "no arc is armed before anybody aims one");
assert(t0.drawn === 0 && t0.marker === false, "and nothing is drawn in the world");
assert(near(t0.speed, 10.0), "the throw's speed is 10 m/s");
assert(near(t0.maxSlopeDegrees, 45.0), "and the steepest standable face is 45 degrees");
assert(vr.teleport() === false, "vr.teleport() refuses with no arc armed");
assert(app.lastError().indexOf("no arc is armed") >= 0,
       "...and says so in words: " + app.lastError());
assert(vr.teleport({ cancel: true }) === false, "so does a cancel");

// ---- 1. A LEVEL THROW LANDS ON THE LOCKED FLOOR -------------------------
//
// THE LOCK IS AN EDIT GUARD, AND A TELEPORT IS NOT AN EDIT (the lead's read of
// the first round, item 3). A new project's ground ships LOCKED — the owner's
// own model, and people lock a floor for exactly one reason: so that clicking
// it does not SELECT it (services/defaultfloor.cpp sets pickable false). A
// floor you cannot select is still a floor you stand on, so the arc's trace
// asks the picker with the lock FORCED OFF, and the commonest throw in the
// editor — at the floor in front of you, in a fresh project — lands on it.
var floorPickable = scene.raycast({ x: 0, y: 3, z: 0 }, { x: 0, y: -1, z: 0 });
assert(floorPickable.length === 0,
       "the project's floor is LOCKED — the document's own raycast refuses it");
var floorAny = scene.raycast({ x: 0, y: 3, z: 0 }, { x: 0, y: -1, z: 0 },
                             { includeUnpickable: true });
assert(floorAny.length > 0, "...and it is there (" + floorAny[0].name + ")");

aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
var t1 = tp();
console.log("teleport, armed level: " + JSON.stringify(t1));
assert(t1.armed === true, "pushing the dominant stick FORWARD arms the arc");
assert(t1.landed === true, "the throw found somewhere to land");
assert(t1.valid === true, "...and it is somewhere a person may stand");
assert(t1.reason === "", "so there is no refusal to report");
show("the landing", t1.landing);
assert(near(t1.landing.y, 0.0, 5e-3),
       "it lands ON the locked ground (which sits a hair above y = 0)");
// THE HIT IS WHERE THE STRAIGHT PIECE MET THE SURFACE, which is up to a couple
// of centimetres short of where the CURVE crosses it (the curve is traced as
// twenty straight pieces): 0.5343 s of flight is 5.343 m along the aim, and
// the chord through that piece meets y = 0 at 5.32. That is the right answer —
// the hit is ON the floor, which is what a landing means — and the case below
// shows the exact arithmetic where there is no geometry to pick.
assert(near(t1.landing.z, -2.32, 0.03),
       "...5.32 m along the aim: the chord of the piece that crosses the floor");
assert(near(t1.landing.x, 0.0, 1e-4), "and straight ahead (the aim had no yaw)");
assert(near(t1.normal.y, 1.0, 1e-3), "the floor's normal is straight up");

// ---- 1b. WITH NO GEOMETRY AT ALL, THE WEARER'S OWN FLOOR CONTINUES ------
//         (the lead's read, item 4)
//
// A throw that met nothing falls back to a PLANE, and the first round used the
// world's y = 0 — a guess about the content, which on a scene built on a raised
// floor or on terrain would land the wearer in mid-air and report it GREEN. The
// plane is the floor the WEARER IS STANDING ON: the rig's own y. With no
// session there is no wearer and no floor of theirs, so it is the world's y = 0
// — which is also what this gate measures, and where the exact arithmetic
// shows: 0.5343 s, 5.3425 m, solved rather than sampled.
var floorId = floorAny[0].id;
node.remove(floorId);
editor.frame(2);
assert(scene.raycast({ x: 0, y: 3, z: 0 }, { x: 0, y: -1, z: 0 },
                     { includeUnpickable: true }).length === 0,
       "the floor is out of the scene entirely");
aim({ x: 0, y: 1.4, z: 3, stickY: 0 });
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
var t1b = tp();
show("the landing with nothing under the arc at all", t1b.landing);
assert(t1b.landed === true && t1b.valid === true,
       "the throw still lands: the floor the wearer stands on continues");
assert(near(t1b.landing.y, 0.0, 1e-4),
       "at y = 0 — with no session there is no rig, and the world's floor is the only plane "
       + "a document can mean");
assert(near(t1b.landing.z, -2.3425, 5e-3),
       "...and at the CURVE's own crossing, 5.3425 m: the plane is solved, not sampled");
assert(near(t1b.normal.y, 1.0, 1e-6), "with an upward normal");
aim({ x: 0, y: 1.4, z: 3, stickY: 0 });

// THE WEARER CAN SEE IT: the curve is line geometry in the world, on the two
// helper channels (in the eyes and in the desktop editor's picture, in no probe
// capture and no user screenshot — vrarc.h).
assert(t1.points >= 3, "the curve is sampled into pieces (" + t1.points + " points)");
assert(t1.drawn === t1.points - 1,
       "...and every piece of it is a line node in the world (" + t1.drawn + " drawn)");
assert(t1.marker === true, "with the landing ring standing at the end of it");
assert(t1.arms === 1, "one throw armed");

// AIMING IT SOMEWHERE ELSE RE-TRACES: the same gesture, a new answer.
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
var armsBeforeTurn = tp().arms;
aim({ x: 0, y: 1.4, z: 3, yaw: 90, stickY: 1 });
var t1d = tp();
show("the landing after turning the wrist 90 degrees", t1d.landing);
assert(near(t1d.landing.x, -5.3425, 5e-3),
       "yawed 90 degrees the throw lands 5.343 m down -X (the right-handed turn about +Y)");
assert(t1d.arms === armsBeforeTurn, "and re-aiming an armed arc is not a second arm");

// ---- 2. LETTING THE STICK GO TAKES THE LANDING --------------------------
//
// ...AND HERE, WITH NO SESSION, THERE IS NOWHERE TO PUT ANYBODY. The refusal is
// the assertion: `Engine::setVrOrigin` does nothing without a session, so a
// teleport outside one moves nobody and says which of the three things went
// wrong.
aim({ x: 0, y: 1.4, z: 3, stickY: 0 });
var t2 = tp();
console.log("teleport after the release: " + JSON.stringify(t2));
assert(t2.armed === false, "letting the stick go ends the aim");
assert(t2.drawn === 0 && t2.marker === false, "and the arc is out of the world again");
assert(t2.teleports === 0, "nobody was moved (there is no rig without a session)");

// THE VERB SAYS SO, in words, and names the honest reason.
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
assert(tp().armed === true, "armed again");
assert(vr.teleport() === false, "vr.teleport() takes the landing and refuses");
assert(app.lastError().indexOf("nobody is in VR") >= 0,
       "...because nobody is in VR: " + app.lastError());
assert(tp().armed === false, "and the arc is put away either way");

// ---- 3. A WALL IS REFUSED, AND THE ARC GOES RED -------------------------
//
// A cube's +Z face is vertical: its normal is 0 degrees from level, which is
// 90 from up and therefore steeper than the 45 every engine's character
// controller draws the line at. The arc still ENDS there — that is where the
// throw hits — and the landing is refused.
var wall = scene.addPrimitive("Cube");
node.transform(wall, { position: { x: 0, y: 1, z: -1.5 } });   // spans y 0..2, z -2.5..-0.5
editor.frame(2);
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
var t3 = tp();
console.log("teleport at a wall: " + JSON.stringify(t3));
assert(t3.armed === true && t3.landed === true, "the throw lands ON the cube's face");
show("where it struck", t3.landing);
assert(near(t3.landing.z, -0.5, 0.05), "on its +Z face at z = -0.5");
assert(t3.valid === false, "...and the landing is REFUSED");
assert(t3.reason.indexOf("steeper") >= 0, "the reason names the slope: " + t3.reason);
assert(near(t3.normal.y, 0.0, 0.05), "the face's normal is level (0 up), i.e. a wall");
assert(t3.marker === true, "the ring still marks where the throw ended");
assert(t3.drawn >= 2, "and the curve is still drawn (red, not absent)");
assert(vr.teleport() === false, "letting go of a refused landing moves nobody");
assert(app.lastError().indexOf("steeper") >= 0,
       "...and the verb repeats the reason: " + app.lastError());

// ---- 4. A RAISED PLATFORM IS A FLOOR ------------------------------------
//
// The same level throw, with a thin slab in the way: the arc descends through
// y = 0.55 at 0.4163 s, which is z = 3 - 4.163 = -1.163 — inside the slab — so
// it lands ON the slab and NOT on the floor plane two thirds of a metre below.
node.transform(wall, { position: { x: 0, y: 0.5, z: -1.5 },
                       scale: { x: 1, y: 0.05, z: 1 } });    // 2 x 0.1 x 2, top y = 0.55
editor.frame(2);
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
var t4 = tp();
console.log("teleport onto a platform: " + JSON.stringify(t4));
assert(t4.landed === true && t4.valid === true, "a throw onto a slab is a standable landing");
show("the landing on the slab", t4.landing);
assert(near(t4.landing.y, 0.55, 0.02), "it lands on the slab's TOP face at y = 0.55");
assert(near(t4.landing.z, -1.163, 0.05), "...where the curve crosses it: z = -1.163");
assert(near(t4.normal.y, 1.0, 0.02), "the slab's top normal is up");
assert(t4.reason === "", "and nothing is refused");
aim({ x: 0, y: 1.4, z: 3, stickY: 0 });

// A LOCKED PLATFORM IS STILL A PLATFORM (the lead's read, item 3): the lock
// stops a CLICK from selecting it, and standing on a thing is not selecting it.
// The first round read the flag on the wrong axis and passed locked geometry
// through, which in a project where the author has locked the set — the normal
// way to work — meant teleporting through the floor of every room.
node.setProperty(wall, "pickable", false);
editor.frame(2);
aim({ x: 0, y: 1.4, z: 3, stickY: 0 });
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
var t4b = tp();
show("the landing with the slab locked", t4b.landing);
assert(t4b.landed === true && t4b.valid === true, "a LOCKED slab is landed on exactly the same");
assert(near(t4b.landing.y, 0.55, 0.02), "at the same y = 0.55 on its top face");
assert(near(t4b.landing.z, -1.163, 0.05), "...and the same place along the curve");
assert(scene.raycast({ x: 0, y: 3, z: -1.5 }, { x: 0, y: -1, z: 0 }).length === 0,
       "...while the document's own raycast still refuses it, which is what the lock is FOR");
aim({ x: 0, y: 1.4, z: 3, stickY: 0 });
node.remove(wall);
editor.frame(2);

// ---- 5. A THROW WITH NOTHING UNDER IT ----------------------------------
//
// Aimed at the sky the marker is still in the air when its two seconds are up
// (a 10 m/s throw from 1.4 m takes 2.17 s to come back to the floor), so there
// is no landing at all — the arc is red and the release moves nobody.
aim({ x: 0, y: 1.4, z: 3, pitch: 89, stickY: 1 });
var t5 = tp();
console.log("teleport at the sky: " + JSON.stringify(t5));
assert(t5.armed === true, "the arc is still armed — the wearer is aiming, badly");
assert(t5.landed === false, "but the throw reaches nothing");
assert(t5.valid === false, "so the landing is refused");
assert(t5.reason.indexOf("nothing to stand on") >= 0, "the reason says so: " + t5.reason);
assert(t5.marker === false, "and NO ring is drawn — there is no landing to mark");
assert(t5.drawn >= 2, "the curve itself is still drawn, red, to where it got");
aim({ x: 0, y: 1.4, z: 3, pitch: 89, stickY: 0 });
assert(tp().armed === false, "the release put it away and moved nobody");

// ---- 6. THE TWO WAYS TO SAY NO ------------------------------------------

var cancels = tp().cancels;
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
assert(tp().armed === true, "armed");
aim({ x: 0, y: 1.4, z: 3, stickY: 1, menu: true });
assert(tp().armed === false, "`menu` held while aiming CANCELS the throw");
assert(tp().cancels === cancels + 1, "...counted as a cancel");
// AND IT STAYS CANCELLED while the stick is still forward: the wearer said no.
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
assert(tp().armed === false, "it does not re-arm while the stick is still over");
aim({ x: 0, y: 1.4, z: 3, stickY: 0 });
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
assert(tp().armed === true, "...and arms again once the stick has come back through centre");
aim({ x: 0, y: 1.4, z: 3, stickY: -1 });
assert(tp().armed === false, "a flick BACKWARDS cancels too (the other way to say no)");
assert(tp().teleports === 0, "and nobody has been moved by any of it");

// ---- 6b. THE THROW IS TAKEN ON THE WAY BACK, NOT ON THE WAY DOWN -------
//         (the lead's read, item 5)
//
// ONE THRESHOLD IS A DOUBLE FIRE. The first round armed at the dead zone (0.5)
// and fired the instant the stick read below it, so a thumb RESTING on the edge
// — which is where a thumb rests, and where a worn stick's noise lives — armed
// and fired on alternating frames. There are two thresholds now, the snap
// turn's own pair: the arc is ARMED at 0.5 and TAKEN only once the stick has
// come back through 0.2, and in the band between it simply stays up and keeps
// re-aiming, which is what a wearer moving their thumb slowly is doing.
var arms6 = tp().arms;
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
assert(tp().armed === true, "armed at full deflection");
aim({ x: 0, y: 1.4, z: 3, stickY: 0.45 });
assert(tp().armed === true, "0.45 — below the dead zone, still aiming: the arc stays up");
aim({ x: 0, y: 1.4, z: 3, stickY: 0.3 });
assert(tp().armed === true, "0.30 — still in the band, still up");
aim({ x: 0, y: 1.4, z: 3, stickY: 0.55 });
assert(tp().armed === true, "0.55 — back over the dead zone, and it did NOT re-arm as a second "
       + "throw");
assert(tp().arms === arms6 + 1, "...one arm for the whole wobble, not four");
aim({ x: 0, y: 1.4, z: 3, stickY: 0.15 });
assert(tp().armed === false, "0.15 — through the re-arm band: THAT is when the throw is taken");
assert(tp().teleports === 0, "(it moves nobody here: no session, no rig)");
aim({ x: 0, y: 1.4, z: 3, stickY: 0 });

// ---- 6c. A TRACKING BLIP HOLDS THE ARC WHERE IT IS ---------------------
//         (the lead's read, item 8)
//
// The grab's own rule, one line of which reads "a skipped locate is not a
// released trigger". The first round CANCELLED on an unlocated frame and
// re-armed on the next, so one dropped frame of a controller threw the curve
// away, reset the counters, and a stick held across a blink could not be
// released into a teleport at all.
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
// THE COUNTERS AS THEY STAND WITH THE THROW ARMED — the blip must move
// neither: an arm again would mean it was thrown away and re-traced, a cancel
// would mean it was thrown away full stop.
var arms8 = tp().arms;
var cancels8 = tp().cancels;
var armedAt = tp().landing;
assert(tp().armed === true, "armed");
// THE AIM STOPS BEING LOCATED for a frame, with the hand still reporting and
// the stick still over — which is exactly what a runtime says when
// `xrLocateSpace` on the aim space misses a frame. (A hand that stops
// reporting ALTOGETHER is held the same way inside step(); on this injection
// route it cannot be shown, because a script that withdraws its only hand ends
// the whole interaction by design — stage 1's "it lets go when the hands do".)
assert(vr.inject("right", { valid: true, aim: { valid: false },
                            grip: { x: 0, y: 1.4, z: 3 },
                            stick: { x: 0, y: 1 } }) === true && vr.step() === true,
       "one frame with the AIM unlocated and the stick still forward");
var blip = tp();
console.log("the arc across a blip: " + JSON.stringify({ armed: blip.armed, drawn: blip.drawn,
                                                         arms: blip.arms, cancels: blip.cancels }));
assert(blip.armed === true, "the arc is STILL armed — a blip is not the wearer letting go");
assert(near(blip.landing.z, armedAt.z, 1e-6), "...at exactly the landing it had");
assert(blip.drawn >= 2, "and still drawn in the world");
assert(blip.arms === arms8 && blip.cancels === cancels8,
       "nothing was armed again and nothing was cancelled");
// ...AND THE THROW IS STILL THERE TO BE TAKEN when the hand comes back.
aim({ x: 0, y: 1.4, z: 3, stickY: 0 });
assert(tp().armed === false, "the release after the blip takes it");
assert(tp().cancels === cancels8, "...as a throw, not as a cancel");

// ---- 7. A HAND THAT IS HOLDING SOMETHING IS NOT AIMING A THROW ---------
//
// That stick belongs to the gesture while something is held: its Y pushes a far
// grab in and out and its X turns it. One stick, one job at a time.
var cube = scene.addPrimitive("Cube");
node.transform(cube, { position: { x: 0, y: 1, z: 0 } });
editor.frame(2);
aim({ x: 0, y: 1, z: 1.7 });                     // 0.7 m from the face: a near grab
assert(vr.grab() === true, "the hand takes hold of the cube");
aim({ x: 0, y: 1, z: 1.7, grab: 1, stickY: 1 });
assert(tp().armed === false, "a full stick forward arms NO arc while something is held");
assert(vr.teleport({ arm: true }) === false, "and the verb refuses, for the same reason");
assert(app.lastError().indexOf("grab") >= 0, "...naming it: " + app.lastError());
aim({ x: 0, y: 1, z: 1.7, grab: 0, stickY: 1 });
assert(vr.interactionMode().grabbing === false, "the cube is put down");
assert(tp().armed === false,
       "...and the stick that was pushing it does not become a throw on the way (it must "
       + "come back through centre first)");
aim({ x: 0, y: 1, z: 1.7, stickY: 0 });
node.remove(cube);
editor.frame(2);

// ---- 8. FOCUS LOSS PUTS THE ARC AWAY -----------------------------------
//
// The runtime took the input away (its own dashboard came up). A throw the
// wearer can no longer see must not be taken — the same rule a gesture in
// flight follows.
aim({ x: 0, y: 1.4, z: 3, stickY: 1 });
assert(tp().armed === true, "armed");
aim({ x: 0, y: 1.4, z: 3, stickY: 1, focused: false });
var t8 = tp();
assert(t8.armed === false, "a focus loss cancels the aim");
assert(t8.drawn === 0, "and takes the curve out of the world");
aim({ x: 0, y: 1.4, z: 3, stickY: 0 });

// ---- 9. THE VERBS DRIVE THE SAME CODE THE STICK DOES -------------------

assert(vr.teleport({ arm: true }) === true, "vr.teleport({arm:true}) aims a throw");
var t9 = tp();
assert(t9.armed === true && t9.valid === true, "...and it is the same armed, valid arc");
assert(near(t9.landing.z, -2.343, 5e-3), "landing at the same place the stick's throw found");
assert(vr.teleport({ arm: true, hand: "left" }) === false,
       "a hand that is not reporting cannot aim one");
assert(vr.teleport({ cancel: true }) === true, "vr.teleport({cancel:true}) puts it away");
assert(tp().armed === false, "and it is away");
// A NAMED PLACE NEEDS NO ARC AND TAKES NO SLOPE TEST — a script that says where
// means it. It still needs somebody in a headset to move.
assert(vr.teleport({ to: { x: 5, y: 0, z: -5 } }) === false,
       "vr.teleport({to}) refuses with nobody in VR");
assert(app.lastError().indexOf("nobody is in VR") >= 0, "...in words: " + app.lastError());

// ---- 10. THE SESSION'S OWN REPORT CARRIES IT TOO -----------------------

assert(vr.state().teleport !== undefined,
       "vr.state().teleport answers as well — 'where am I about to go' is a question about "
       + "the session as much as about the controllers");
assert(vr.state().teleport.speed === tp().speed, "...the same map, from the one object");

// ---- 11. MALFORMED CALLS THROW; ANSWERS DO NOT -------------------------

throws(function () { vr.teleport({ nosuch: 1 }); }, "nosuch",
       "an unknown teleport key throws, naming it");
throws(function () { vr.teleport({ to: { x: 1, nope: 2 } }); }, "nope",
       "an unknown key inside `to` throws too");
throws(function () { vr.teleport({ to: { x: 1 }, cancel: true }); }, "not two",
       "two of the forms at once is refused rather than guessed");
throws(function () { vr.teleport({ arm: true, hand: "middle" }); }, "middle",
       "an unknown hand throws, naming what was passed");

// ---- 12. AND THE EDITOR IS EXACTLY AN EDITOR AFTERWARDS ---------------

editor.frame(2);
assert(vr.state().active === false, "no session was ever started by any of this");
assert(tp().armed === false && tp().drawn === 0, "no arc is left in the world");
assert(tp().teleports === 0, "and nobody was ever moved on a box with no headset");

console.log("scripting.e2e.vr_teleport: PASS");
