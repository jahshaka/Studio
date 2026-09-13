// scripting.e2e.scene_issues — THE SCENE-ERROR AREA (owner decisions Q1b/Q1c,
// SPECS/SUN_AND_LIGHT_DEFAULTS_SPEC.md).
//
// THE RULE, which is the whole design: if the person using the editor can fix
// it in their scene, it is an issue and it is shown over the viewport; if it
// exists for us to debug the engine, it stays in the log. So every issue here
// NAMES the object it is about (selectably), says what to DO about it, is
// dismissible, and NEVER REPEATS while the condition holds.
//
// API-first: the store and its verbs are the model (editor.issues /
// raiseIssue / dismissIssue / clearIssue / checkScene); the viewport bar is a
// view of exactly this and adds nothing. That is why this suite can drive the
// whole facility without a window.
//
// THE TWO FIRST CUSTOMERS, both owner-reported:
//   sun.tie      two directional lights on the same Forward Shading Priority,
//                so which one is the sun comes out of a tie-break the author
//                never chose (duplicating the sun does exactly this);
//   shadow.leak  a light whose shadows are switched off standing close enough
//                to solid geometry to light straight through it — the
//                Showroom's lamp above a sealed roof, which nothing ever
//                mentioned to the owner.
//
// AND THE CASE THAT IS NOT AN ISSUE: a scene with no directional light at all.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}
function throws(fn, msg) {
    var threw = false;
    try { fn(); } catch (e) { threw = true; console.log("ok: " + msg + " (" + e.message + ")"); }
    if (!threw) throw new Error("assert failed (no error): " + msg);
}
function issuesOfKind(list, kind) {
    return list.filter(function (i) { return i.kind === kind; });
}

// ---- registry -------------------------------------------------------------
var editor_ = api.verbs().filter(function (m) { return m.module === "editor"; })[0];
var names = editor_.verbs.map(function (v) { return v.name; });
["issues", "raiseIssue", "dismissIssue", "clearIssue", "checkScene"].forEach(function (v) {
    assert(names.indexOf(v) >= 0, "editor." + v + " is registered");
});

var guid = project.create("Scene issues " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- the facility itself: raise / never repeat / dismiss / clear ----------
var cube = scene.addPrimitive("cube", { position: { x: 0, y: 0, z: 0 } });
editor.clearIssue("demo:" + cube);   // a clean slate whatever ran before

var id = editor.raiseIssue({ kind: "demo", node: cube,
                             message: "This is a made-up problem.",
                             action: "Do the made-up thing about it." });
assert(id === "demo:" + cube, "raiseIssue builds the id from the kind and the object: " + id);
var live = issuesOfKind(editor.issues(), "demo");
assert(live.length === 1, "the issue is live");
assert(live[0].node === cube, "...it NAMES the object (a guid a script or a click can select)");
assert(live[0].nodeName !== "", "...and remembers its name: '" + live[0].nodeName + "'");
assert(live[0].action !== "", "...and says what to do about it");
assert(live[0].dismissed === false, "...and is showing");

// NEVER REPEATS. This is the property a scanner running every second depends
// on: raising the same id again changes nothing at all.
editor.raiseIssue({ kind: "demo", node: cube, message: "A different message entirely." });
live = issuesOfKind(editor.issues(), "demo");
assert(live.length === 1, "raising the same issue again does NOT add a second row");
assert(live[0].message === "This is a made-up problem.",
       "...and does not overwrite the message that is already up");

// DISMISS hides it but keeps it live, so nothing can nag with it again.
assert(editor.dismissIssue(id), "dismissIssue");
assert(issuesOfKind(editor.issues(false), "demo").length === 0, "...it is gone from the viewport");
assert(issuesOfKind(editor.issues(true), "demo").length === 1, "...but it is still LIVE");
editor.raiseIssue({ kind: "demo", node: cube, message: "Nagging." });
assert(issuesOfKind(editor.issues(false), "demo").length === 0,
       "...so re-raising it cannot bring it back");

// CLEAR forgets it: the condition is gone, and the NEXT occurrence is news.
assert(editor.clearIssue(id), "clearIssue");
assert(issuesOfKind(editor.issues(true), "demo").length === 0, "...it is forgotten");
editor.raiseIssue({ kind: "demo", node: cube, message: "Back again." });
assert(issuesOfKind(editor.issues(false), "demo").length === 1,
       "...and the same condition later is shown again");
editor.clearIssue(id);

// ---- refusals: an issue with nothing to say helps nobody ------------------
throws(function () { editor.raiseIssue({ message: "no kind" }); }, "raiseIssue needs a kind");
throws(function () { editor.raiseIssue({ kind: "demo" }); }, "raiseIssue needs a message");
assert(!editor.dismissIssue("no-such-issue"), "dismissing an unknown id is false, not a throw");

// ---- customer 1: TWO SUNS -------------------------------------------------
// The scene starts with one directional light. A second AUTO-SLOTS to priority
// 1 and is not a problem at all — that is the whole point of the auto-slot.
var first = scene.nodes().filter(function (n) {
    return node.info(n.id).type === "light" && node.property(n.id, "lightType") === 1;
})[0].id;
var second = scene.addLight("directional", { position: { x: 4, y: 6, z: 0 }, name: "Moon" });
var after = editor.checkScene();
assert(issuesOfKind(after.list, "sun.tie").length === 0,
       "a SECOND directional light is not an error — it auto-slots to priority 1");

// Now make them fight, which is exactly what duplicating the sun does.
node.setProperty(second, "forwardShadingPriority", 0);
var tie = editor.checkScene();
var ties = issuesOfKind(tie.list, "sun.tie");
assert(ties.length === 1, "two directionals at the same priority raise ONE issue");
assert(ties[0].node === second || ties[0].node === first,
       "...naming one of the two lights: " + ties[0].nodeName);
assert(ties[0].message.indexOf("priority") >= 0, "...and saying what the problem is");
assert(ties[0].action.indexOf("Forward Shading Priority") >= 0,
       "...and telling the user to change its priority");
assert(tie.raised.length >= 1, "checkScene reports what it raised for the first time");

// NEVER REPEATS, through the scanner: a second run raises nothing new.
var again = editor.checkScene();
assert(again.raised.length === 0, "a second scan raises NOTHING new (the never-repeat rule)");
assert(issuesOfKind(again.list, "sun.tie").length === 1, "...and there is still exactly one row");

// FIX THE SCENE AND IT GOES AWAY — and then comes back if you break it again,
// dismissed or not.
node.setProperty(second, "forwardShadingPriority", 1);
var fixed = editor.checkScene();
assert(issuesOfKind(fixed.list, "sun.tie").length === 0, "fixing the priorities clears the issue");
node.setProperty(second, "forwardShadingPriority", 0);
var broken = editor.checkScene();
assert(issuesOfKind(broken.list, "sun.tie").length === 1, "...and breaking it again raises it anew");
node.setProperty(second, "forwardShadingPriority", 1);
editor.checkScene();

// ---- customer 2: A LIGHT THAT SHINES THROUGH SOMETHING --------------------
// The owner's Showroom: a lamp above a sealed roof lit the floor through it,
// and nothing in the editor ever said so.
var roof = scene.addPrimitive("plane", { position: { x: 0, y: 3, z: 0 } });
node.transform(roof, { scale: { x: 6, y: 1, z: 6 } });
var lamp = scene.addLight("point", { position: { x: 0, y: 5, z: 0 }, name: "Roof Lamp" });
node.setProperty(lamp, "distance", 20);
var clean = editor.checkScene();
assert(issuesOfKind(clean.list, "shadow.leak").length === 0,
       "a lamp that CASTS shadows over a roof is not an issue — the roof stops it");

node.setProperty(lamp, "shadowMapType", 0);          // 0 = off (fill light)
var leaked = editor.checkScene();
var leaks = issuesOfKind(leaked.list, "shadow.leak");
assert(leaks.length === 1, "switching its shadows off raises the leak issue");
assert(leaks[0].node === lamp, "...naming the LIGHT, which is the thing to fix");
assert(leaks[0].message.indexOf("through") >= 0, "...and saying it reaches through something");
assert(leaks[0].action.indexOf("Shadow Type") >= 0, "...and telling the user how to stop it");

node.setProperty(lamp, "shadowMapType", 2);          // 2 = soft
assert(issuesOfKind(editor.checkScene().list, "shadow.leak").length === 0,
       "turning its shadows back on clears it");

// ---- THE BAR IS AN EDITOR SURFACE (CLEANUP-1 item 3) ----------------------
// The store is the model and works everywhere; the BAR is a frameless
// always-on-top window over the editor's viewport, and it used to float over
// the Desktop, Assets, Player, Materials and Publish pages complete with a
// Select button that selected in a viewport nobody was looking at. The 1 Hz
// scanner's comment claimed a space check for a week; there was none.
//
// editor.issueBar() runs one scan-and-decide pass and reports, so this is not a
// race against that timer.
app.space("editor");
node.setProperty(lamp, "shadowMapType", 0);          // something for it to say
var bar = editor.issueBar();
assert(bar.editorActive === true, "the editor is the active space");
assert(bar.rows > 0, "there is an issue to show (" + bar.rows + ")");
assert(bar.visible === true, "the bar is up over the editor viewport");

// Two spaces are enough: the rule is "the current space is not the editor", not
// a list — and the Player page stands a second engine scene up, which is a lot
// of machinery for a check about one window's visibility.
["desktop", "assets"].forEach(function (space) {
    app.space(space);
    var away = editor.issueBar();
    assert(away.editorActive === false, "on the " + space + " page the editor is not active");
    assert(away.visible === false, "...and the scene-issue bar is NOT on screen");
});

app.space("editor");
var back = editor.issueBar();
assert(back.editorActive === true, "back on the editor page");
assert(back.rows > 0 && back.visible === true, "...and the bar comes back with the issue");
node.setProperty(lamp, "shadowMapType", 2);
editor.checkScene();

// ---- ISSUES DO NOT SURVIVE A SCENE CHANGE (CLEANUP-1 item 7) --------------
// `scan` only forgets the two kinds IT owns, so an issue raised by a verb (or
// by any other producer) used to live on across a project open and describe a
// node that is not in the scene any more. Every BIND resets the store now.
editor.raiseIssue({ kind: "carryover", node: roof, message: "Raised in the OLD scene." });
assert(issuesOfKind(editor.issues(true), "carryover").length === 1,
       "an issue of another kind is live in this scene");

// ---- NOT an issue: a scene with no directional light at all ---------------
project.create("Lamps only " + Date.now());
assert(issuesOfKind(editor.issues(true), "carryover").length === 0,
       "opening another scene FORGETS it (the store is scene-scoped)");
scene.nodes().forEach(function (n) {
    if (node.info(n.id).type === "light" && node.property(n.id, "lightType") === 1)
        node.remove(n.id);
});
var lampsOnly = editor.checkScene();
assert(issuesOfKind(lampsOnly.list, "sun.tie").length === 0,
       "an interior lit by lamps alone raises NO issue about having no sun");
assert(world.sun().light === "", "...and it really has no sun");

console.log("scene issues e2e: all checks passed");
