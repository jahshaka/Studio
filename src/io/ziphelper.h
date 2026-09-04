/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ZIPHELPER_H
#define ZIPHELPER_H

// The ONE zip loop (ASSET_PIPELINE preflight amendment 7): every archive
// producer/consumer calls these instead of hand-rolling zip_entry loops.
//
// THREADING (STABILITY_PROGRAM_SPEC Lane 4): both entry points are pure file
// work — no database, no widgets, no Qt event loop — so they are the half of
// an archive operation that belongs on a worker thread. Both take an optional
// per-entry callback so the worker can report progress and be cancelled
// between entries; the callback runs on the CALLING thread, which for a
// threaded archive is the worker, so it must not touch widgets.

#include <QString>
#include <functional>

namespace ZipHelper
{
/// Per-entry callback: (entry name, 1-based index, total entries when known,
/// otherwise 0). Return false to ABANDON the operation — zipDirectory then
/// deletes the partial archive, extract leaves whatever it had written in the
/// caller's temp directory. Never called with a null name.
using EntryFn = std::function<bool(const QString &name, int index, int total)>;

/// Zip a directory's contents (recursively, relative entry names, empty
/// directories included) into zipPath. Overwrites zipPath.
bool zipDirectory(const QString &dirPath, const QString &zipPath, QString *errorOut = nullptr,
                  const EntryFn &onEntry = EntryFn());

/// Extract an archive into an existing directory.
bool extract(const QString &zipPath, const QString &destDir, QString *errorOut = nullptr,
             const EntryFn &onEntry = EntryFn());
} // namespace ZipHelper

#endif // ZIPHELPER_H
