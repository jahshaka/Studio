/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef BOOTREPORT_H
#define BOOTREPORT_H

// THE BOOT's FACTS, AS RESULTS (NOQT-CORE-1 E2; the lead's ruling: a tool never reads a log line). What the boot
// did to the library - the shipped meshes' seed, the one-time index gate, the indexes an existing library's
// rows reached the opening thread without, the stale bakes the background sweep planned - recorded by the code
// that produced each fact and read by `app.bootReport()`. Memory only (no durable write on the UI thread).
//
// ONE per process, owned by the shell's lifecycle (shell/shelllifecycle.h: the facts land while MainWindow is
// built, before the services container exists); StudioServices points at it. Every field but `staleRebuilds` is
// written on the UI thread before any script runs; `staleRebuilds` is written by the sweep's worker when it has
// planned, so it is an atomic the worker shares (it holds its own reference: the report may die first).

#include "services/primitiveassets.h"

#include <QtGlobal>

#include <atomic>
#include <memory>

struct BootReport
{
    /// The boot's seedAll (shell/shelllifecycle.cpp openLibrary): rows created, re-seeded by name, failed.
    PrimitiveAssets::SeedResult seed;
    /// The one-time index gate before the shell opened the library (app/main.cpp): statements built, its wall.
    struct IndexGate { int built = 0; qint64 ms = 0; } indexGate;
    /// Index statements run over an EXISTING library's rows on the opening thread (Database::createIndexes):
    /// a defect of the caller, 0 when the gate did its job.
    int indexAtOpen = 0;
    /// The stale bakes the background sweep planned to rebuild (MeshBakeStore::startBackgroundRebuild);
    /// -1 until the sweep has planned (or when no sweep ran), so a not-yet is never read as a 0.
    std::shared_ptr<std::atomic<int>> staleRebuilds = std::make_shared<std::atomic<int>>(-1);
};

#endif // BOOTREPORT_H
