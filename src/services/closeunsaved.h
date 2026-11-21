/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLOSEUNSAVED_H
#define CLOSEUNSAVED_H

/// WHAT A CLOSE DOES WITH UNSAVED CHANGES (STUDIO-HYGIENE-1 audit) — every route that closes a world names one,
/// so no route discards edits silently. The close (MainWindow::settleUnsaved) settles BEFORE it touches the world:
/// with auto-save on it saves, and a save that FAILS, or that left out the nodes a run added (the scene stays
/// dirty), is NOT settled - nor is a dirty scene with auto-save off. An unsettled world then goes by this:
enum class CloseUnsaved {
    Ask,      ///< a person's route (a tile, the Close button, the menu, New Scene, the window's close): Save (again,
              ///< until it holds) / Discard / Cancel, the reason in the box. In a SCRIPTED session (a --script or
              ///< --headless run, an --mcp-port client: FirstRun::isScriptedSession) nobody can answer a box, so it
              ///< refuses as Refuse does; a forced data root alone is a person's session and is asked.
    Refuse,   ///< a script's route (project.open/openAsync/close/create/openSample, app.quit): refused with a
              ///< sentence unless the caller passed {discard: true}.
    Discard   ///< the caller DECIDED the edits go: a verb's explicit {discard: true}, the library reset. Nothing
              ///< is saved, not even by auto-save.
};

/// What MainWindow::settleUnsaved does, as a function of what it found (a pure function, tested on the CPU in
/// services.core). `scripted` = FirstRun::isScriptedSession(); `saveHeld` = with auto-save on, the save held the
/// whole document (unread with auto-save off, where `dirty` decides).
enum class UnsavedVerdict { Close, Ask, Refuse };
inline UnsavedVerdict decideUnsaved(CloseUnsaved route, bool scripted, bool autoSave, bool dirty, bool saveHeld)
{
    if (route == CloseUnsaved::Discard) return UnsavedVerdict::Close;
    if (autoSave ? saveHeld : !dirty) return UnsavedVerdict::Close;
    return (route == CloseUnsaved::Ask && !scripted) ? UnsavedVerdict::Ask : UnsavedVerdict::Refuse;
}

#endif // CLOSEUNSAVED_H
