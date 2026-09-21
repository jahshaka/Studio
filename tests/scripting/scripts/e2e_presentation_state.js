// scripting.e2e.presentation_state — the viewport's presentation state machine,
// the thing that decides whether the editor viewport shows the engine's frames
// or its loading cover.
//
// RENAMED from e2e_viewport_cover.js, ASSERTIONS UNCHANGED, when the Qt cover
// widget was deleted (owner decision D2, SPECS/STATS_OVERLAY_SPEC.md §6). That
// nothing here had to change is the point: this suite always drove
// editor.viewportState(), never the widget, so it is the regression gate that
// says the state machine survived the port into the engine intact.
//
// THE DEFECT (2026-09-03, owner-sighted twice): opening a world switched to the
// editor page before the engine had presented anything into the viewport's
// native window, so the X server kept showing the pixels that were there
// before — a perfect copy of the desktop page — for seconds. The fix is a
// deliberate cover, and the cover is driven entirely by what this script
// asserts: editor.viewportState().
//
// Phase A: the state is a pure function of the presented-frame count.
// Phase B: opening a world RESTARTS that count at zero, even though a project
//          close/open reuses the engine scene underneath (the engine's own
//          counter does not restart there — the viewport rebases it).
// Phase C: the first present is not enough (a Vulkan present is queued); the
//          second one reveals the viewport.
//
// PHASES A-C RUN WITH THE COVER ON (lane OPEN-COVER-2b). The cover became a
// PREFERENCE, default OFF (SPECS/OPEN_COVER_SPEC.md §3, owner's pick), and ON
// is defined as "today's contract, byte for byte" — so this suite, which IS
// that contract, switches it on and asserts exactly what it always asserted.
// Phase D below is the other half: with the cover off, the same open, and what
// changes.

function assert(cond, msg) {
    if (!cond) throw new Error("assert failed: " + msg);
    console.log("ok: " + msg);
}

var STATES = ["presenting", "loading", "noscene", "offscreen"];
var REVEAL = 2;             // EngineSceneViewport::kPresentsBeforeReveal

function state(tag) {
    var v = editor.viewportState();
    assert(STATES.indexOf(v.state) >= 0, tag + ": known state (" + v.state +
        ", frames=" + v.framesPresented + ")");
    return v;
}

// THE COVER IS A PREFERENCE NOW, and its default is OFF (see the header).
assert(editor.loadingCover() === false, "the loading cover is OFF by default");
assert(editor.loadingCover(true) === true, "editor.loadingCover(true) switches it on");

var name = "Viewport Cover " + Date.now();
var guid = project.create(name);
assert(guid.length > 10, "project.create -> " + guid);

var st = state("after create");
assert(typeof st.framesPresented === "number", "framesPresented is a number");

if (st.state === "offscreen") {
    // No on-screen render target in this session (a document-only stand-in, or
    // the macOS offscreen fallback): there is nothing to cover. Say so, and
    // keep the invariant that still applies.
    console.log("viewport is offscreen in this session — cover states not applicable");
    editor.frame(2);
    assert(editor.viewportState().state === "offscreen", "offscreen is stable");
} else {
    // ---- phase A: state IS the count ----
    assert(st.state === (st.framesPresented >= REVEAL ? "presenting" : "loading"),
        "the state is a pure function of the presented-frame count");

    editor.frame(3);
    var stepped = state("after frame(3)");
    assert(stepped.framesPresented === st.framesPresented + 3,
        "every stepped frame is one present (" + st.framesPresented + " -> " +
        stepped.framesPresented + ")");
    assert(stepped.state === "presenting", "a stepped world presents");

    // ---- phase B: opening a world restarts it ----
    assert(project.save(), "project.save");
    assert(project.close(), "project.close");

    // A5a (ENGINEERING_DEBT_SPEC addendum 5): closing a project must RELEASE
    // the world. MainWindow::closeProject never called removeScene() — its one
    // caller was the open path — so the viewport kept the closed world's scene,
    // mirror and datablocks alive behind the desktop and this state was
    // unreachable through the ordinary close. "noscene" is the whole assertion:
    // it is the state the viewport reports only when nothing is bound.
    var closed = state("after close");
    assert(closed.state === "noscene",
        "project.close releases the viewport's world (got " + closed.state + ")");

    assert(project.open(name), "project.open(" + name + ")");

    var reopened = state("after reopen");
    assert(reopened.framesPresented === 0,
        "opening a world restarts the count at 0, so the cover goes back up " +
        "(got " + reopened.framesPresented + ")");
    assert(reopened.state === "loading",
        "and the viewport is loading, not presenting stale pixels");

    // ---- phase C: one present is not enough, two are ----
    editor.frame(1);
    var one = state("after frame(1)");
    assert(one.framesPresented === 1, "one present counted");
    assert(one.state === "loading",
        "one present does not reveal the viewport: a Vulkan present is queued, " +
        "so the frame just counted is not certainly the one on screen");

    editor.frame(1);
    var two = state("after frame(1) again");
    assert(two.framesPresented === 2, "two presents counted");
    assert(two.state === "presenting", "the second present reveals the viewport");
}


// ---- phase D: THE COVER OFF (OPEN_COVER_SPEC §2.1/§3, lane OPEN-COVER-2b) ----
//
// The owner's pick: no panel for a load. The world appears at once and streams
// in behind one line at the bottom of the viewport. THREE things change, and
// nothing else does.
if (editor.viewportState().state !== "offscreen") {
    assert(editor.loadingCover(false) === false, "editor.loadingCover(false) switches it off");

    // (1) THE PANEL IS NEVER DRAWN FOR A LOAD. `cover` is what is on screen —
    //     the reading exists because a preference whose whole job is drawing a
    //     panel would otherwise need a photograph to test.
    var second = "Viewport Cover B " + Date.now();
    assert(project.create(second).length > 10, "a second world, created with the cover off");
    var made = editor.viewportState();
    assert(made.cover === "none",
        "no cover is up after a create with the preference off (got '" + made.cover + "')");
    assert(made.loadingCover === false, "...and viewportState says which preference it was");

    // (2) THE REVEAL'S FRAMES BELONG TO THE NEW WORLD — the never-the-stale-frame
    //     rule (§2.1 item 1, §6.1). The two inline presents at the reveal exist
    //     because a Vulkan present into a not-yet-mapped child window does not
    //     survive the map; with the cover ON they draw the PANEL and are rebased
    //     away (phase B: framesPresented is 0 after an open). With it OFF they
    //     draw the WORLD, after setScene, and they COUNT — which is the same
    //     statement as "what is on screen is not the previous world's last
    //     frame", made in a number instead of a photograph.
    assert(made.framesPresented >= REVEAL,
        "the reveal presented the new world, not a stale frame (framesPresented " +
        made.framesPresented + " >= " + REVEAL + ")");
    assert(made.state === "presenting",
        "...so the viewport is presenting as soon as the create returns");

    // (3) THE READINGS BEHIND THE INDICATOR have a shape a caller can rely on,
    //     in every session and at every moment — including this one, where a
    //     scripted run has just rendered COMPLETE frames and nothing is owed.
    var p = editor.viewportState().pending;
    var keys = ["shaders", "textures", "gi", "shadersThisLoad", "shadersExpected",
                "texturesThisLoad"];
    for (var i = 0; i < keys.length; i++)
        assert(typeof p[keys[i]] === "number", "pending." + keys[i] + " is a number");
    assert(typeof editor.viewportState().streaming === "boolean", "streaming is a boolean");
    assert(typeof editor.viewportState().indicator === "string", "indicator is a string");
    assert(editor.viewportState().streaming === (p.shaders + p.textures + p.gi > 0),
        "streaming is exactly 'something is still owed'");

    // A SCRIPTED FRAME IS NOT A STREAMING FRAME (§2.1 item 4, the safety rule
    // every pixel suite depends on): stepping frames here renders each one to
    // completion, so nothing can be left owed by one.
    editor.frame(3);
    var after = editor.viewportState();
    assert(after.pending.gi === 0,
        "a scripted frame builds the lighting arm to completion, never a stage of it");
    assert(after.cover === "none", "and still no cover");

    // (4) AND THE PREFERENCE SURVIVES A ROUND TRIP through the one capability
    //     the Preferences row also calls (services/loadingcover.h).
    assert(editor.loadingCover(true) === true, "cover on");
    assert(editor.viewportState().loadingCover === true, "...and viewportState agrees");
    assert(editor.loadingCover(false) === false, "cover off again");
}

console.log("viewport cover state machine: OK");
