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
/// so no route discards edits silently. The close (MainWindow::closeWorld) settles BEFORE it touches the world:
/// with auto-save on it saves, and a save that FAILS refuses the close whatever this says (the world stays; the
/// save door has told the user once). With auto-save off and the scene dirty (UndoService::isDirty):
enum class CloseUnsaved {
    Ask,      ///< a person's route (a tile, the Close button, the menu, New Scene): Save / Discard / Cancel. In a
              ///< DRIVEN session (a suite, a script, an MCP client: FirstRun::isDrivenSession) nobody can answer a
              ///< box, so it refuses as Refuse does.
    Refuse,   ///< a script's route (project.open/openAsync/close/create): refused with a sentence unless the
              ///< caller passed {discard: true}.
    Discard   ///< the caller DECIDED the edits go: a verb's explicit {discard: true}, the library reset. Nothing
              ///< is saved, not even by auto-save.
};

#endif // CLOSEUNSAVED_H
