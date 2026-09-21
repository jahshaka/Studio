#ifndef LOADINGCOVER_H
#define LOADINGCOVER_H

// THE LOADING COVER, AS A PREFERENCE (SPECS/OPEN_COVER_SPEC.md §3, lane
// OPEN-COVER-2b; owner's pick, ledger §904: "let's see how the streaming
// looks").
//
// The cover is the flat grey panel with "Loading world…" on it that the engine
// draws over the viewport from the moment a world starts loading until its
// second present (viewport/enginesceneviewport.cpp). It exists because opening
// a world used to arrive in ONE frame and the window showed whatever was there
// before while it did.
//
// SINCE OPEN-COVER-2a THE WORLD NO LONGER ARRIVES IN ONE FRAME: the create and
// the open run one slice per event-loop turn, and the first GI arm is built one
// stage per driver frame of a world the user can already see. So the cover now
// hides a world that is there — which is why it is OFF by default and the
// world appears at once, streaming in behind one indicator line.
//
// ON is TODAY'S CONTRACT, byte for byte (§3 "ON (a)"): the panel from
// `beginSceneLoad` until `kPresentsBeforeReveal` presents, then the stream-in
// exactly as it is now. Nothing else about a load changes with this switch —
// the pace rule, `Scene::setLoading`, the slices and the present counting are
// the same either way, and `editor.viewportState().state` reads the same
// "loading"/"presenting" in both.
//
// WHAT IS NOT A PREFERENCE: `Cover::NoScene` ("No world open", the Desktop's
// empty viewport). That is not a load — it is a statement about a window with
// nothing in it — and it is always drawn. (Always RAISED was true from the
// start; always DRAWN only since STALE-VIEW-1: a view with no scene bound had
// no workspace, so the panel changed not one pixel and the region showed the
// app's watermark. A scene-less view owns a clear-only workspace now.)
//
// ONE CAPABILITY, three callers: `editor.loadingCover()` (the verb, with its
// test), Preferences › General (the row), and `beginSceneLoad` (which reads it
// ONCE per load, so a preference changed mid-load cannot make the cover flicker
// on and off between two frames of it).

namespace loadingcover {

/// The persisted key (SettingsManager / jahsettings.ini). Absent = OFF.
inline const char *settingsKey() { return "loading_cover"; }

/// Whether a load should be covered. False by default — see the note above.
bool enabled();
/// Persists the choice. Takes effect at the NEXT load, by design.
void setEnabled(bool on);

}   // namespace loadingcover

#endif   // LOADINGCOVER_H
