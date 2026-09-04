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

console.log("viewport cover state machine: OK");
