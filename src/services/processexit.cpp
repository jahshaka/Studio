/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/processexit.h"

#include "data/settingsmanager.h"
#include "services/mainthreadwatchdog.h"

#include "jahshaka/engine/ProcessExit.h"

#include <cstdio>
#include <cstdlib>

namespace processexit {
namespace {
bool gTaken = false;
int gCode = 0;
}   // namespace

int takeFastExit(int rc)
{
    jahshaka::engine::markProcessExit();
    gTaken = true;
    gCode = rc;
    return rc;
}

End::~End()
{
    if (!gTaken) return;
    // THE EXIT HANDLERS, BY NAME (std::_Exit runs none): the settings store's last write reaches its file (the dock
    // layout ~MainWindow saved), and the watchdog thread is down (a no-op when finalizeAppExit stopped it).
    MainThreadWatchdog::stop();
    SettingsManager::getDefaultManager()->settings->shutdown();   // the atexit handler's body (settingsmanager.h)
    std::fflush(nullptr);
    std::_Exit(gCode);
}

}   // namespace processexit
