/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef THUMBNAILSTOP_H
#define THUMBNAILSTOP_H

// "Stop the thumbnail sweep" — one flag, on its own, so that the two places
// that must be able to SET it (the app's shutdown path and ScriptEngine::stop)
// do not have to link the thing that reads it (THUMBS-1 fix round F1).
//
// WHY A SWEEP MUST BE STOPPABLE AT ALL. `thumbrebuild::rebuildMissing` yields
// to the event loop between assets, and a yield delivers a WINDOW CLOSE:
// MainWindow::closeEvent runs with the sweep on the stack, shutdownBackgroundWork
// destroys the thumbnail renderer, and the loop would otherwise carry on — re-
// creating the renderer, rendering the rest of the library with the window
// gone, and finally opening a modal box on a dead window (the "modal swallowed
// the quit" zombie, ended only by the 20 s force-exit).
//
// STICKY, and the sweep does NOT clear it: a stop that lands a moment before a
// sweep starts — a queued gesture delivered after the close, which is exactly
// the shape of this defect — must stop that sweep too. It is cleared by the
// things that mean "a new intent, on a live app": a script run starting
// (ScriptEngine::evaluate) and the Assets page's own menu entry. Nothing
// legitimate starts a sweep after the app has begun shutting down.

namespace thumbrebuild
{

/// Ask any running (or about to start) sweep to stop at its next row.
void requestStop();
/// True while a stop is outstanding.
bool stopRequested();
/// Clear it. Called where a NEW intent begins on a live app: the start of a
/// script run, and the Assets page's "Rebuild missing thumbnails" action.
void clearStop();

/// IS A SWEEP IN FLIGHT? A sweep yields to the event loop between rows, so
/// anything a yield can deliver — a verb, a button — can arrive with it on
/// the stack, and one of those (`app.resetLibrary`) is about to delete the
/// rows it is writing (services/libraryreset.h). The flag lives here with the
/// stop for the same reason the stop does: the readers must not have to link
/// the sweep. Set by `rebuildMissing` around its loop, and re-entrant safe
/// (it is a counter).
bool sweepRunning();

/// RAII for the flag above; `rebuildMissing` is its only user.
class SweepMark
{
public:
    SweepMark();
    ~SweepMark();
    SweepMark(const SweepMark &) = delete;
    SweepMark &operator=(const SweepMark &) = delete;
};

}   // namespace thumbrebuild

#endif   // THUMBNAILSTOP_H
