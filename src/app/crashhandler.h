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

/// Tells the handler where this run's SESSION log is, so the crash report can
/// name it and so the marker below has somewhere to go
/// (SESSION_LOG_SPEC §6, §3.8-4). Call once, right after JahLog::start().
///
/// TWO POINTERS AND NOTHING ELSE. The path is copied into a static char[] and
/// the descriptor into a static int, because the handler runs in SIGNAL
/// CONTEXT: it may call write(2) and backtrace_symbols_fd and nothing else.
/// It must NEVER call JahLog — that takes a mutex and allocates, which is the
/// malloc-in-a-signal-handler deadlock this whole file exists to avoid
/// (spec §9-R3). A builder "improving" the handler to use the nice new logger
/// reintroduces it.
void crashHandlerSetSessionLog(const char *path, int fd);

#endif // CRASHHANDLER_H
