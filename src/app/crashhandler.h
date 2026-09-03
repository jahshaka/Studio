/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

/// Installs async-signal-safe handlers for the fatal signals. On a crash it
/// writes crash-<pid>-<time>.log beside the app's working directory (signal,
/// fault address, native backtrace), then re-raises so the default action —
/// and any future breakpad — still runs. STABILITY_AUDIT.md §5 action item 1:
/// before this, the app captured NOTHING (no handlers, apport eats the cores),
/// which is why the ~1/65 startup crash went undiagnosed for days.
void installCrashHandler();

#endif // CRASHHANDLER_H
