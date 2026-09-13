// scripting.e2e.scene_issues — THE SCENE-ERROR AREA (owner decisions Q1b/Q1c,
// SPECS/SUN_AND_LIGHT_DEFAULTS_SPEC.md).
//
// THE RULE, which is the whole design: if the person using the editor can fix
// it in their scene, it is an issue and it is shown over the viewport; if it
// exists for us to debug the engine, it stays in the log. So every issue here
// NAMES the object it is about, says what to DO about it, and NEVER REPEATS
// while the condition holds.
//
// AND IT IS READ-ONLY (owner, 2026-09-13, lane BAR-1): "get rid of the Select
// button and the button next to it so it just shows the error, let the user fix
// it", and "it should just list all errors in the scene line by line if there
// are multiple". So there is no dismiss — not a verb, not a button — the bar
// has NO buttons at all, and every live issue is a line of its own in a stable
// order. A line leaves when the 1 Hz scanner finds the condition gone, and that
// is the only way it leaves.
//
// API-first: the store and its verbs are the model (editor.issues /
// raiseIssue / clearIssue / checkScene); the viewport bar is a view of exactly
// this and adds nothing. That is why this suite can drive the whole facility
// without a window.
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
["issues", "raiseIssue", "clearIssue", "checkScene", "issueBar"].forEach(function (v) {
    assert(names.indexOf(v) >= 0, "editor." + v + " is registered");
});
// THE DELETED VERB (CRUD law): dismiss is gone from the registry and from the
// object — an issue is fixed, never waved away.
assert(names.indexOf("dismissIssue") < 0, "editor.dismissIssue is GONE from the registry");
assert(typeof editor.dismissIssue === "undefined", "... and off the object too");

var guid = project.create("Scene issues " + Date.now());
assert(guid.length > 10, "project.create -> " + guid);

// ---- the facility itself: raise / never repeat / clear --------------------
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
assert(live[0].dismissed === undefined, "...and carries no 'dismissed' field any more");

// NEVER REPEATS. This is the property a scanner running every second depends
// on: raising the same id again changes nothing at all.
editor.raiseIssue({ kind: "demo", node: cube, message: "A different message entirely." });
live = issuesOfKind(editor.issues(), "demo");
assert(live.length === 1, "raising the same issue again does NOT add a second row");
assert(live[0].message === "This is a made-up problem.",
       "...and does not overwrite the message that is already up");

// CLEAR forgets it: the condition is gone, and the NEXT occurrence is news.
// It is the ONLY way an issue leaves — there is nothing to wave it away with.
assert(editor.clearIssue(id), "clearIssue");
assert(issuesOfKind(editor.issues(), "demo").length === 0, "...it is forgotten");
editor.raiseIssue({ kind: "demo", node: cube, message: "Back again." });
assert(issuesOfKind(editor.issues(), "demo").length === 1,
       "...and the same condition later is shown again");
editor.clearIssue(id);

// ---- refusals: an issue with nothing to say helps nobody ------------------
throws(function () { editor.raiseIssue({ message: "no kind" }); }, "raiseIssue needs a kind");
throws(function () { editor.raiseIssue({ kind: "demo" }); }, "raiseIssue needs a message");
assert(!editor.clearIssue("no-such-issue"), "clearing an unknown id is false, not a throw");

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

// FIX THE SCENE AND IT GOES AWAY — and then comes back if you break it again.
node.setProperty(second, "forwardShadingPriority", 1);
var fixed = editor.checkScene();
assert(issuesOfKind(fixed.list, "sun.tie").length === 0, "fixing the priorities clears the issue");
node.setProperty(second, "forwardShadingPriority", 0);
var broken = editor.checkScene();
assert(issuesOfKind(broken.list, "sun.tie").length === 1, "...and breaking it again raises it anew");
node.setProperty(second, "forwardShadingPriority", 1);
editor.checkScene();

// ---- sky.duplicate: a SECOND Sky Light (SKY_LIGHT_SPEC.md §2) --------------
// A scene's ambient is the FIRST VISIBLE Sky Light; a second one is inert — it
// sits in the outliner with an intensity the user can drag and does nothing at
// all. That is exactly what this bar is for, and it has to CLEAR by itself both
// ways the user can fix it.
var skyOne = world.skyLight();
assert(skyOne.light !== "", "the scene ships with a Sky Light");
assert(issuesOfKind(editor.checkScene().list, "sky.duplicate").length === 0,
       "one Sky Light is not an issue");

var skyTwo = scene.addLight("sky", { name: "Sky Light 2" });
var dup = editor.checkScene();
var dups = issuesOfKind(dup.list, "sky.duplicate");
assert(dups.length === 1, "a SECOND Sky Light raises ONE issue");
assert(dups[0].node === skyTwo, "...naming the inert one, not the live one");
assert(dups[0].message.indexOf("Sky Light") >= 0, "...and saying what it is");
assert(dups[0].action.toLowerCase().indexOf("hide") >= 0,
       "...and offering the two fixes: " + dups[0].action);
assert(issuesOfKind(editor.checkScene().list, "sky.duplicate").length === 1,
       "a second scan raises nothing new and still shows the one row");

// FIX 1: HIDE IT. A hidden Sky Light does not light, so it does not compete.
assert(node.setProperty(skyTwo, "visible", false), "hide the second Sky Light");
assert(issuesOfKind(editor.checkScene().list, "sky.duplicate").length === 0,
       "hiding it clears the issue");
assert(world.skyLight().light === skyOne.light, "...and the first one is still the skylight");

// ...and it comes back when the user shows it again.
assert(node.setProperty(skyTwo, "visible", true), "show it again");
assert(issuesOfKind(editor.checkScene().list, "sky.duplicate").length === 1,
       "showing it raises the issue anew");

// FIX 2: DELETE IT.
assert(node.remove(skyTwo), "delete the second Sky Light");
assert(issuesOfKind(editor.checkScene().list, "sky.duplicate").length === 0,
       "deleting it clears the issue too");
assert(world.skyLight().count === 1, "...and the scene has exactly one Sky Light again");

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

// ---- EVERY ISSUE IS A LINE, AND THE BAR HAS NO BUTTONS (lane BAR-1) -------
// The owner's two decisions about the error area, asserted through the shell
// seam: `lines` is what the widget actually built (one QLabel per issue, plus a
// "+N more" line past the eighth) and `buttons` is a live walk of the bar's
// children for QAbstractButton — zero, forever. Select and dismiss are gone:
// the message quotes both objects by name, so there is nothing to click.
app.space("editor");
node.setProperty(lamp, "shadowMapType", 0);          // something for it to say
var bar = editor.issueBar();
assert(bar.editorActive === true, "the editor is the active space");
assert(bar.rows > 0, "there is an issue to show (" + bar.rows + ")");
assert(bar.visible === true, "the bar is up over the editor viewport");
assert(bar.buttons === 0, "THE BAR HAS NO BUTTONS — no Select, no dismiss");
assert(bar.lines === bar.rows, "one line per issue (" + bar.lines + " for " + bar.rows + ")");

// TWO ISSUES AT ONCE = TWO LINES, in the store's stable order (by kind, then by
// the object): the leak first, the sun tie second, whichever the scanner found
// first. This is the case that used to collapse into "and 1 more" behind a
// three-row cap.
node.setProperty(second, "forwardShadingPriority", 0);   // the sun tie, again
var both = editor.issueBar();
var live = editor.issues();
assert(both.rows === 2 && both.lines === 2,
       "two simultaneous issues are TWO lines (" + both.lines + ")");
assert(both.buttons === 0, "... still with no buttons");
assert(live.length === 2, "editor.issues() lists both");
assert(live[0].kind === "shadow.leak" && live[1].kind === "sun.tie",
       "... in a STABLE order, by kind: " + live[0].kind + " then " + live[1].kind);
var order = editor.issues().map(function (i) { return i.id; }).join("|");
editor.checkScene();                                     // another pass, same two conditions
assert(editor.issues().map(function (i) { return i.id; }).join("|") === order,
       "... and a later scan does not reshuffle them (" + order + ")");

// FIX ONE AND EXACTLY ITS LINE GOES, within one pass of the scanner (which is
// what editor.issueBar() runs). The other one is untouched and keeps its text.
var leakId = live[0].id, tieId = live[1].id;
node.setProperty(second, "forwardShadingPriority", 1);   // fix the sun tie only
var one = editor.issueBar();
var rest = editor.issues();
assert(one.rows === 1 && one.lines === 1, "fixing one issue leaves ONE line");
assert(rest.length === 1 && rest[0].id === leakId,
       "... and it is exactly the OTHER one (" + rest[0].kind + ")");
assert(rest.map(function (i) { return i.id; }).indexOf(tieId) < 0,
       "... the fixed issue's line is gone");

// MANY ISSUES: a line each up to the cap, then ONE trailing count. The cap is
// generous (8) because an author with eight broken things wants to see eight,
// but an error area that can grow without bound would cover the viewport it is
// reporting on.
for (var d = 0; d < 10; ++d)
    editor.raiseIssue({ kind: "demo" + d, node: lamp,
                        message: "Demo problem " + d + ".", action: "Fix demo " + d + "." });
var many = editor.issueBar();
assert(many.rows === 11, "eleven live issues (" + many.rows + ")");
assert(many.lines === 9, "... shown as 8 lines plus one 'and N more' line (" + many.lines + ")");
assert(many.buttons === 0, "... and still no buttons");
for (var d2 = 0; d2 < 10; ++d2) editor.clearIssue("demo" + d2 + ":" + lamp);
assert(editor.issueBar().lines === 1, "clearing them leaves the one real issue");

// ---- THE BAR IS AN EDITOR SURFACE (CLEANUP-1 item 3) ----------------------
// The store is the model and works everywhere; the BAR is a frameless
// always-on-top window over the editor's viewport, and it used to float over
// the Desktop, Assets, Player, Materials and Publish pages, describing a scene
// nobody was looking at. The 1 Hz scanner's comment claimed a space check for a
// week; there was none. editor.issueBar() runs one scan-and-decide pass and
// reports, so this is not a race against that timer.
//
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
assert(issuesOfKind(editor.issues(), "carryover").length === 1,
       "an issue of another kind is live in this scene");

// ---- NOT an issue: a scene with no directional light at all ---------------
project.create("Lamps only " + Date.now());
assert(issuesOfKind(editor.issues(), "carryover").length === 0,
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
