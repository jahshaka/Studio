/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/thumbnailstop.h"

#include <atomic>

namespace thumbrebuild
{
namespace
{
/// std::atomic because ScriptEngine::stop() is called from the console/MCP
/// side while the sweep runs on the main thread.
std::atomic<bool> gStopRequested{false};

/// How many sweeps are in flight (a counter, not a bool: a sweep yields, and
/// a yield can deliver a gesture that starts another one).
std::atomic<int> gSweepsRunning{0};
}   // namespace

void requestStop() { gStopRequested.store(true); }
bool stopRequested() { return gStopRequested.load(); }
void clearStop() { gStopRequested.store(false); }

bool sweepRunning() { return gSweepsRunning.load() > 0; }

SweepMark::SweepMark() { gSweepsRunning.fetch_add(1); }
SweepMark::~SweepMark() { gSweepsRunning.fetch_sub(1); }

}   // namespace thumbrebuild
