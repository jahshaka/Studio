/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#ifndef SHIPPEDASSETS_H
#define SHIPPEDASSETS_H

// SHIPPED ASSETS THROUGH THE CAS (plan item 15c, platform audit D35).
//
// The app ships a handful of image files that end up INSIDE a user's scene:
// the default ground's tile, the default particle image, the material
// presets' maps and the six cube-sky presets. Until this lane every one of
// them reached a project the pre-pipeline way — `QFile::copy` into the
// project folder plus a bare catalog row with no stored bytes — and the
// scene writer and both readers kept a by-NAME fallback
// (`Database::fetchAssetGUIDByName` / `projectFolder + row name`) whose only
// job was to find those copies again. The copies were never in the store, so
// a project export left them behind (the Mirror Room and Showroom samples
// ship a fileless "Tile.png" row to this day), and the fallbacks were the
// last by-name references in the document path.
//
// A shipped file is now an ORDINARY library texture: imported through the
// ONE import pipeline the first time any project needs it, identified by its
// CONTENT (the sha256 the store already keys on — so a second project, a
// re-import by hand or a sample archive carrying the same bytes all resolve
// to the same row), and pinned into the project as a BINDING
// (ProjectAssets::AddKind::Binding: it is referenced by something in the
// scene, so no companion material is minted). The document then holds the
// pinned store object's path, which is exactly what the writer's CAS lookup
// and the readers' pin-first resolution already understand. No file is ever
// written into a project folder.

#include <QString>
#include <QStringList>
#include <QVector>

class Database;
class Project;

namespace ShippedAssets
{

struct Pinned
{
    /// The library Texture row. EMPTY when no project is open (the startup
    /// placeholder, a script with no project.create): there is nothing to pin
    /// into and that session never saves, so `path` is the shipped file.
    QString guid;
    /// The file to render: the pinned store object in a project, else the
    /// shipped file itself.
    QString path;
    /// Non-empty on failure (a missing source file, an import refusal).
    QString error;
    /// True when THIS call created the project's pin (the project did not pin
    /// the row before) — what an undo of the call has to take back.
    bool newlyPinned = false;
    bool ok() const { return error.isEmpty() && !path.isEmpty(); }
};

/// `sourcePath` — a file the app ships, or any image file — as a LIBRARY
/// texture pinned into `project`. `displayName` names the row the first time
/// it is imported (empty = the file's own name); an existing row keeps
/// whatever it is called. Idempotent: the same bytes answer the same row.
Pinned pinTexture(const QString &sourcePath, const QString &displayName,
                  Database *db, Project *project);

/// The shipped cube-sky presets (app/content/skies/alternative/<dir>/).
struct SkyPreset
{
    QString name;          ///< what the Presets panel and world.skyPreset call it
    QString directory;     ///< absolute folder holding the six faces
    QString extension;     ///< the faces' shared suffix (jpg / png)
    /// Absolute face files in CUBEMAP SLOT ORDER: front, back, left, right,
    /// top, bottom — the order the sky panel's slots and world.sky's keys use.
    QStringList faces() const;
    /// The front face — the tile the Presets panel shows.
    QString thumbnail() const;
};

/// Every shipped cube sky, in the order the Presets panel lists them.
QVector<SkyPreset> skyPresets();

/// The six faces of the preset called `name` (case-insensitive) pinned into
/// `project`, as guids in slot order (front, back, left, right, top, bottom).
/// Empty + `errorOut` on an unknown name, no open project, or a face the
/// pipeline refused. Face rows are named "<preset>_<face>.<ext>"
/// ("cove_front.jpg"), the names the panel always gave them.
QStringList pinSkyPreset(const QString &name, Database *db, Project *project,
                         QString *errorOut = nullptr);

} // namespace ShippedAssets

#endif // SHIPPEDASSETS_H
