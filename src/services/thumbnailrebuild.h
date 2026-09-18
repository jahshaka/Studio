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
#include <memory>

#include "services/thumbnailstop.h"

class Database;
class Project;

namespace jahshaka { namespace engine { class Engine; } }

namespace thumbrebuild
{

struct Outcome
{
    bool ok = false;
    QString reason;      ///< empty when ok
    /// There is nothing to draw for this row and never was: the row stores NO
    /// definition at all (a builtin primitive's Object row — the default
    /// Ground — is a document thing with an empty `asset` column). It is a
    /// failure to the caller who asked for THIS guid and a SKIP to a sweep,
    /// which must not report the floor of every project as broken.
    ///
    /// NOT the same thing as a model whose stored BYTES are gone (fix round
    /// F6): that row has a definition, the content behind it is missing, and
    /// the user needs to be told — it is a failure with its reason.
    bool nothingToDraw = false;
    static Outcome good() { Outcome o; o.ok = true; return o; }
    static Outcome bad(const QString &why) { Outcome o; o.reason = why; return o; }
};

/// Rebuild and store the thumbnail of ONE asset, whatever its type.
///
/// `engine` is HANDED IN, not fetched: this is a service, the engine belongs to
/// the app that owns it, and a function that reaches for a singleton cannot be
/// tested without one. Null is legal — the document-only types (image, audio,
/// video, clip, file, photometric profile) render nothing and work headless;
/// the rest answer `{ok:false, reason}` saying the engine is not running.
Outcome rebuildOne(Database *db, Project *project, const QString &guid,
                   const std::shared_ptr<jahshaka::engine::Engine> &engine);

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
    /// The sweep stopped early because a stop was requested (the window is
    /// closing, the script was stopped). Whatever it had already rebuilt is
    /// stored; the caller must NOT report a result to a person.
    bool cancelled = false;
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
    /// An EXTRA stop source, on top of requestStop() (a script's own stop
    /// flag, a progress dialog's Cancel). Checked once per row.
    std::function<bool()> cancelled;
};

// requestStop() / stopRequested() live in services/thumbnailstop.h — one flag
// on its own, so the app's shutdown path and ScriptEngine::stop can SET it
// without linking everything that reads it. ThumbnailGenerator::shutdown()
// calls it (which is what puts it on the window-close / aboutToQuit path) and
// so does ScriptEngine::stop.

/// The sweep. `yield` runs between assets — the caller keeps the UI alive with
/// it (a rebuild is one engine render per asset and a library can hold
/// hundreds; no sweep may hold the UI thread for the whole of it).
SweepResult rebuildMissing(Database *db, Project *project,
                           const std::shared_ptr<jahshaka::engine::Engine> &engine,
                           const SweepOptions &options,
                           const std::function<void()> &yield = {});

}   // namespace thumbrebuild

#endif   // THUMBNAILREBUILD_H
