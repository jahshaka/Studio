/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef THUMBNAILREBUILD_H
#define THUMBNAILREBUILD_H

// thumbrebuild — THE body of "(re)build this asset's thumbnail", for every
// library type, and the sweep that repairs the rows that have none.
//
// WHY IT EXISTS (THUMBS-1). `assets.refreshThumbnail` carried this switch
// inside the verb, so nothing else could run it: the bulk repair the owner
// needs for tiles already stored grey would have been a second copy, and the
// Avatar rows nobody could rebuild at all had no branch to add one to. The
// verb is now a two-line call onto rebuildOne(), the bulk form is a LOOP over
// the same function, and the Assets page's menu entry calls the same loop.
//
// A FAILURE ALWAYS HAS A REASON. Every branch answers with `{ok, reason}` —
// the verb hands the reason to the script, the menu entry puts it in a box,
// the sweep collects one per guid. A thumbnail that fails is never silent.
//
// Engine-bound types (Object, ParticleSystem, Material, Shader, Avatar) render
// through the ONE borrowed EngineThumbnailRenderer; the document-only types
// (Texture, Music, Video, Animation, File) need no engine and work headless.

#include <QString>
#include <QStringList>
#include <QVector>
#include <functional>

class Database;
class Project;

namespace thumbrebuild
{

struct Outcome
{
    bool ok = false;
    QString reason;      ///< empty when ok
    /// There is nothing to draw for this row and never was — a builtin
    /// primitive's Object row (the default Ground) stores no model. It is a
    /// failure to the caller who asked for THIS guid and a SKIP to a sweep,
    /// which must not report the floor of every project as broken.
    bool nothingToDraw = false;
    static Outcome good() { Outcome o; o.ok = true; return o; }
    static Outcome bad(const QString &why) { Outcome o; o.reason = why; return o; }
};

/// Rebuild and store the thumbnail of ONE asset, whatever its type. The engine
/// is taken from EngineHost when the type needs one.
Outcome rebuildOne(Database *db, Project *project, const QString &guid);

/// True when this row's stored thumbnail is missing or undecodable — what
/// "missing" means to the sweep, and what the user sees as a grey tile.
bool thumbnailIsMissing(Database *db, const QString &guid);

struct Failure
{
    QString guid;
    QString reason;
};

struct SweepResult
{
    int considered = 0;   ///< rows the sweep looked at
    int rebuilt = 0;
    int skipped = 0;      ///< rows with nothing to draw (see Outcome::nothingToDraw)
    QStringList rebuiltGuids;   ///< so a caller can refresh exactly those tiles
    QVector<Failure> failed;
};

struct SweepOptions
{
    /// Only rows whose stored thumbnail is missing or undecodable (the repair
    /// the owner needs). False rebuilds every supported row.
    bool missingOnly = true;
    /// Limit to the open project's assets. False sweeps the whole library.
    bool projectOnly = false;
    /// 0 = no limit.
    int limit = 0;
};

/// The sweep. `yield` runs between assets — the caller keeps the UI alive with
/// it (a rebuild is one engine render per asset and a library can hold
/// hundreds; no sweep may hold the UI thread for the whole of it).
SweepResult rebuildMissing(Database *db, Project *project, const SweepOptions &options,
                           const std::function<void()> &yield = {});

}   // namespace thumbrebuild

#endif   // THUMBNAILREBUILD_H
