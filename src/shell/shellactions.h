/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHELLACTIONS_H
#define SHELLACTIONS_H

// THE SHELL'S KEYBOARD ACTIONS (EDITOR_SHORTCUTS_SPEC §1; D10-SHELL-MODULES):
// every row the shell's own parts answer — the tools, the camera and the views,
// the overlays, playback, snapping, the edit chords, the panels and the spaces —
// in the order the Preferences page lists them. Each row's handler calls the
// part that owns the behaviour (the toolbar, the view controller, the editor
// page, the docks, the module hub); a module's or the assistant's rows are
// THEIR contributions, placed among these by their `after` anchors.

class ActionHost;
class MainWindow;

namespace shellactions {

/// Defines the shell's rows (and their space-scoped handlers) on `actions`.
void define(ActionHost &actions, MainWindow &window);

}   // namespace shellactions

#endif // SHELLACTIONS_H
