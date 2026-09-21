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

var coverOn = false;            // what the phases below have the preference set to

// THE SCENE'S SUBMISSION, ONCE IT HAS STOPPED MOVING. A frame's
// `submittedTriangles` is everything every pass handed the GPU, so while a
// world's first lighting arm is converging the probe captures are counted in
// it and two consecutive frames differ for an honest reason. Step until two
// agree — the rule this tree learned from cameras.exposure: a wall-clock settle
// measures nothing in this engine, read until the value stops moving.
function settledTriangles() {
    var last = -1;
    for (var i = 0; i < 240; ++i) {
        editor.frame(1);
        var now = app.renderStats().submittedTriangles;
        if (now === last) return now;
        last = now;
    }
    return last;
}

function state(tag) {
    var v = editor.viewportState();
    assert(STATES.indexOf(v.state) >= 0, tag + ": known state (" + v.state +
        ", frames=" + v.framesPresented + ")");
    // THE ON PATH DRAWS NO LINE (see the header): every reading taken in
    // phases A-C carries the assertion, so there is no moment of a covered
    // load this suite does not look at.
    if (coverOn)
        assert(v.indicator === "", tag + ": no indicator line while the cover is ON (got '" +
            v.indicator + "')");
    return v;
}

// THE COVER IS A PREFERENCE NOW, and its default is OFF (see the header).
//
// PUT IT BACK FIRST, and that is not belt and braces — it is this suite's own
// hygiene (STALE-VIEW-1 finding 4). The preference is PERSISTED in the data
// root this suite keeps between runs, phase A switches it ON, and a run that
// dies anywhere after that line leaves it on: the NEXT run then fails on its
// very first assertion, "the loading cover is OFF by default", with nothing to
// do with the change under test. The default is what an absent key means, so
// writing it off here restores exactly the state a fresh home has, and the
// assertion below still asserts the default — it reads the shipped value back
// through the same verb the Preferences row calls.
editor.loadingCover(false);
assert(editor.loadingCover() === false, "the loading cover is OFF by default");
assert(editor.loadingCover(true) === true, "editor.loadingCover(true) switches it on");

// ...AND WITH IT ON THERE IS NO INDICATOR LINE, EVER (the Fable read, item 1).
// "ON = today's contract, byte for byte" is not byte for byte if the panel
// carries a caption, and the HUD draws the stats text OVER the cover fill by
// design — so a line composed while the cover is up would appear on the grey
// panel and again through the ON stream-in. Asserted at every step of phases
// A-C below, by `state()`.

coverOn = true;
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

    // ---- phase B2: AND A VIEWPORT WITH NO WORLD PRESENTS ITS OWN BACKGROUND
    //      (lane STALE-VIEW-1) ---------------------------------------------
    // Until this lane a view with no scene had no workspace, so it presented
    // NOTHING and the window kept the last frame it was given — the world that
    // had just been closed, with "No world open" raised over it and unable to
    // reach a pixel. `blankPresented` counts the frames the viewport put on
    // screen with no world bound; it is the number that says the panel is
    // actually drawn, and it is the same number phase D differences across a
    // load in place. (The PIXELS are asserted where pixels can be read: an
    // on-screen view refuses readPixels, so tests/engine's
    // `a_scene_less_view_clears_and_still_draws_its_panel` owns that half.)
    assert(closed.cover === "noscene",
        "...and the panel that says so is up (got '" + closed.cover + "')");
    var blankAtClose = closed.blankPresented;
    assert(typeof blankAtClose === "number", "viewportState().blankPresented is a number");
    // PRINTED, NOT ASSERTED, and the reason is the behaviour itself: a plain
    // close switches the window to the Desktop page, the editor viewport is
    // hidden, and a hidden view is DISABLED — it owns no pixels on screen, so
    // it presents nothing and this count does not move. That is right. What
    // this arm is here to catch is the shape and the state; the number is
    // asserted where a world is torn down with the page still up — phase D's
    // load in place, below.
    editor.frame(3);
    var idle = editor.viewportState();
    console.log("   blankPresented across three stepped frames with no world: " +
        blankAtClose + " -> " + idle.blankPresented +
        " (the close switched to the Desktop, so the viewport is hidden)");
    assert(idle.framesPresented === 0,
        "no frame stepped with no world bound is a frame of a world (framesPresented " +
        idle.framesPresented + ")");

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
    coverOn = false;

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
    var keys = ["shaders", "textures", "gi", "shadersThisLoad", "texturesThisLoad"];
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

    // ...AND IT DRAWS NO INDICATOR EITHER, which is a harder rule than it
    // looks and cost this lane a gate (scripting.e2e.atom_lods). THE OVERLAY
    // IS GEOMETRY: its text quads go through the same render queue as the
    // scene, so every character of that line adds draw calls and triangles to
    // `app.renderStats().submittedTriangles` — the number the LOD suites
    // measure a level switch with. A first cut spent the streaming window on
    // DRIVER ticks only, and a --script run has none, so the line came back
    // hundreds of frames after the load on any frame that compiled a shader.
    // After a load the line belongs to the render loop and to nothing else.
    // SETTLE FIRST, THEN MEASURE. `submittedTriangles` is the whole frame's
    // submission across every pass, so while the lighting arm is still
    // converging the probe captures are in it and two consecutive frames
    // legitimately differ. The house rule for this engine is to read until the
    // value stops moving, never to compare two frames chosen by hand.
    var tris = settledTriangles();
    editor.frame(1);
    assert(editor.viewportState().indicator === "",
        "a scripted frame after a load draws NO indicator (got '" +
        editor.viewportState().indicator + "')");
    editor.frame(1);
    assert(app.renderStats().submittedTriangles === tris,
        "...so a stepped frame submits the same geometry it did before (" + tris + ")");

    // (4) A SCRIPTED FRAME TAKEN INSIDE THE LOAD'S OWN WINDOW draws no line
    //     either (the Fable read, item 2). `project.createAsync` returns while
    //     the runner still has slices queued, so `editor.frame(1)` here lands
    //     between the reveal and the first driver tick — exactly the frame the
    //     old condition (`mSceneLoadPending`, lowered only by a driver tick)
    //     put the indicator's quads into. The triangle count is the assertion,
    //     because it is what atom_lods measures a LOD switch with.
    var settled = project.create("Viewport Cover C " + Date.now());
    assert(settled.length > 10, "a third world, to measure against");
    var authored = settledTriangles();
    var asyncGuid = project.createAsync("Viewport Cover D " + Date.now());
    assert(asyncGuid.length > 10, "project.createAsync -> " + asyncGuid);
    var turns = 0;
    while (project.openState() !== "idle" && turns < 4000) { editor.frame(1); ++turns; }
    assert(project.openState() === "idle", "the asynchronous create finished (" + turns + " frames)");
    editor.frame(1);
    assert(editor.viewportState().indicator === "",
        "a scripted frame inside a create's own window draws NO indicator (got '" +
        editor.viewportState().indicator + "')");
    editor.frame(1);
    assert(app.renderStats().submittedTriangles === authored,
        "...and submits the authored geometry, nothing of the overlay (" + authored + " vs " +
        app.renderStats().submittedTriangles + ")");

    // (5) A LOAD IN PLACE, IN A SESSION WHOSE VIEWPORT IS NOT ON SCREEN.
    //     The override that covers an in-place load keys on the viewport being
    //     VISIBLE — which is the whole point of it: a panel is only worth
    //     drawing where a frozen frame would otherwise be seen. A `--script`
    //     run has a viewport that is never shown (measured here: this open
    //     walks noscene -> loading -> presenting with the cover 'none'
    //     throughout, and `presentCovered` returns early for the same reason),
    //     so the override must NOT fire and the preference's OFF must hold end
    //     to end. THE OTHER HALF — a visible viewport, where the cover DOES go
    //     up — is asserted over MCP against the real window, in
    //     open.responsive's streaming arm; it cannot be asserted from here.
    assert(editor.viewportState().loadingCover === false, "the preference is still OFF");
    // THE NEVER-THE-STALE-FRAME NUMBER (lane STALE-VIEW-1). A load in place
    // tears the previous world down and binds the next one some hundreds of
    // milliseconds later; between the two the viewport has NO world, and what
    // it puts on screen in that gap used to be nothing at all — so the window
    // kept the previous world's last frame (measured on the rig: mean
    // |difference| 0.01-0.10 per byte from the frame before the open, i.e. the
    // same picture). It now clears to its own background, and this difference
    // is the assertion that the teardown reached the screen.
    var blankBeforeOpen = editor.viewportState().blankPresented;
    assert(project.openAsync(name), "an open IN PLACE, with the preference off");
    var seen = [], lastSeen = "", sawLine = "";
    for (var g = 0; g < 4000 && project.openState() !== "idle"; ++g) {
        var mid = editor.viewportState();
        var tag = mid.cover + "/" + mid.state;
        if (tag !== lastSeen) { seen.push(tag); lastSeen = tag; }
        if (mid.cover !== "none")
            throw new Error("assert failed: a cover appeared in a session with no visible " +
                "viewport ('" + mid.cover + "')");
        // ONLY BEFORE THE REVEAL. The runner is still finishing when the page
        // switch has already happened, so the tail of this loop is a world
        // that IS on screen — and a line there is the designed behaviour, not
        // a violation. What this asserts is the other half: while nothing of
        // this world is showing, nothing is drawn over it either. (Solo the
        // loop happened to end before the reveal; under a gate's load it did
        // not, which is what made the wider form red at 460/461.)
        if (mid.state !== "presenting" && mid.indicator !== "") sawLine = mid.indicator;
        editor.frame(1);
    }
    console.log("   in-place open saw: " + seen.join(" -> "));
    assert(project.openState() === "idle", "the in-place open finished");
    assert(editor.viewportState().blankPresented > blankBeforeOpen,
        "...and the viewport presented its own background while no world was bound, " +
        "so the previous world's last frame is not what was on screen (" +
        blankBeforeOpen + " -> " + editor.viewportState().blankPresented + ")");
    assert(sawLine === "",
        "...and no indicator line was drawn while nothing of the world was on screen " +
        "(saw '" + sawLine + "')");

    // (6) AND THE PREFERENCE SURVIVES A ROUND TRIP through the one capability
    //     the Preferences row also calls (services/loadingcover.h).
    assert(editor.loadingCover(true) === true, "cover on");
    assert(editor.viewportState().loadingCover === true, "...and viewportState agrees");
    assert(editor.loadingCover(false) === false, "cover off again");
}

console.log("viewport cover state machine: OK");
