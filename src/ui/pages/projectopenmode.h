/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef PROJECTOPENMODE_H
#define PROJECTOPENMODE_H

// WHICH SPACE AN OPEN LANDS IN — stated by the caller that starts the open,
// carried down to MainWindow::openProjectAsync, and never stored as page state
// (SMOKE-FIX-1, 2026-09-18).
//
// It used to be a member (`ProjectManager::openInPlayMode`, a bool) written by
// ONE route — the desktop tile — and read by ALL of them, uninitialised. Every
// sample-browser open and every archive import therefore landed in whatever the
// bool happened to hold: on the owner's box and on the rig that was `true`, so
// opening a sample scene put the user in the Player with `=== PLAY START ===`
// four milliseconds ahead of `=== SCENE OPEN ===`. A route's intent is an
// argument.
//
// It lives in its own header so the RULE below can be asserted by a suite that
// does not build the whole desktop page (ui.project_tile).

enum class ProjectOpenMode {
    Editor,     ///< the normal open: the world lands in the editor
    Player      ///< the tile's Play button, or the user's standing preference
};

namespace projectopen {

/// WHERE A DESKTOP TILE'S OPEN LANDS, and the only place that decides it.
///
/// `playButton` is the tile's own Play control (and the Play entry in its
/// menu): an explicit "play this now", which always means the Player.
///
/// `openInPlayerPreference` is the user's standing answer for a PLAIN open —
/// the double-click and the Open entry — from Preferences → Worlds → "Open
/// Worlds In Player" (`open_in_player`, OFF by default). The row existed for
/// years and nothing read it; the owner asked for it to be real (2026-09-18).
///
/// NOTHING ELSE CONSULTS IT. A sample, an archive import, a new world and
/// `project.openAsync` each state their own space, so this is the one route
/// where "open" is a bare gesture with no space in it — which is exactly why it
/// is the one route a preference can speak for.
inline ProjectOpenMode tileOpenMode(bool playButton, bool openInPlayerPreference)
{
    return (playButton || openInPlayerPreference) ? ProjectOpenMode::Player
                                                  : ProjectOpenMode::Editor;
}

}   // namespace projectopen

#endif // PROJECTOPENMODE_H
