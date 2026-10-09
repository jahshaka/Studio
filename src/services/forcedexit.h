/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SERVICES_FORCEDEXIT_H
#define SERVICES_FORCEDEXIT_H

// THE FORCED EXIT (TESTING-CLEANUP-2 H8c and its fix round). Every path that ends this process
// with std::_Exit because an orderly end cannot be trusted — the shutdown watchdog (a teardown past
// its budget), the worker-reap refusal, and the device-loss end when its dialog is never answered —
// ends it HERE: one log line beginning `shutdown watchdog:` that names which path fired, and exit
// code 86. A forced exit used to be code 0 (a clean quit to every test and gate), and the device-
// loss end could block for ever on a modal box nobody would answer (a spawned app on a rig display:
// alive ~1,100 s after "FATAL … ending the session", TC2-DEVICE-LOST-HANG).

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

namespace forcedexit {

constexpr int kCode = 86;

/// Ends the process now: the line, then _Exit(86). Never returns.
[[noreturn]] inline void now(const std::string &why)
{
    std::fprintf(stderr, "shutdown watchdog: %s — forcing process exit (code %d)\n", why.c_str(), kCode);
    std::fflush(nullptr);
    std::_Exit(kCode);
}

/// A detached watchdog: after `seconds`, `now(why)` — unless the process ended first.
inline void arm(int seconds, std::string why)
{
    std::thread([seconds, why = std::move(why)]() {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        now(why);
    }).detach();
}

}   // namespace forcedexit

#endif   // SERVICES_FORCEDEXIT_H
