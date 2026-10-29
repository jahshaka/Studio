/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SERVICES_PROCESSEXIT_H
#define SERVICES_PROCESSEXIT_H

// THE EXIT-ONLY TEARDOWN (URGENT #102). At a process end the app no longer destroys the Vulkan device: the NVIDIA
// driver's background compile thread holds a lock vkDestroyDevice waits on for the whole of its remaining CPU —
// 10-20 s after a shader change, measured (spikes/photon-2/11-2-teardown/, spikes/urgent-102/) — and three ship rows
// sat at the 60-s cap on it. takeFastExit() marks the process (jahshaka/engine/ProcessExit.h): the
// shutdown order runs exactly as before — every step 1-8, the engine's caches saved and their writer joined, every
// view, scene and mesh released, the database closed — only Ogre's Root (the device, the instance) is kept, and when
// main's End guard goes out of scope the process ends with std::_Exit(rc). NOT exit(): the driver's and Ogre's
// finalizers would run against the live device. The exit handlers this process registers run first, by name
// (End's destructor): an _Exit runs none.
//
// TWO CALL SITES, kept separable on purpose (src/app/main.cpp): the CLI's end (--engine-selftest, --script,
// --script-pool, --mcp-port, --dump-api-docs) and the editor's quit. The editor's may go back to the full destroy
// (the driver stores its re-optimised pipelines only at a clean exit) by deleting its one line.
//
// The in-process destroy-and-re-create never comes here: the app makes its engine once (EngineHost::start is
// idempotent) and destroys it only at its end; test_engine_recreate keeps the real destroy.

namespace processexit {

/// Marks the process for the exit-only teardown and records `rc` as its exit code; returns `rc`. Call it before
/// the engine's last reference drops (the window's teardown) — after it, any engine destroyed keeps its Root.
int takeFastExit(int rc);

/// Declared first in main, so it is destroyed last: when takeFastExit ran, its destructor runs the exit handlers
/// by name (the settings writer's drain + join, the main-thread watchdog's stop), flushes stdio and _Exits with the
/// recorded code. Otherwise it does nothing and main returns as it always did.
class End {
public:
    End() = default;
    End(const End &) = delete;
    End &operator=(const End &) = delete;
    ~End();
};

}   // namespace processexit

#endif   // SERVICES_PROCESSEXIT_H
