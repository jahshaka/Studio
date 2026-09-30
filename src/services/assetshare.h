/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef ASSETSHARE_H
#define ASSETSHARE_H

// ONE ASSET, SHARED — the single-material file (MATERIAL_BUNDLE_SPEC §5 and
// the owner's decision Q5: "now, in phase 2, as the manifest-v2 zip").
//
// "material bundles are great if they are self-contained, makes them
// portable" — the owner's words, and this is the file that proves it: one
// asset plus everything it is made of, openable on a machine that has never
// seen any of it.
//
// THE FORMAT, and why it is made of pieces that already existed:
//
//   <name>.jbundle  (a zip)
//     jah.manifest.json   manifest v2 (src/export/exportmanifest.h), kind =
//                         the asset's own type word ("material"). This is the
//                         self-identifying header: what is in here, which
//                         guids, which files, how big. A human — or another
//                         tool — can read it without unpacking anything else.
//     payload.json        the closure itself: `assetclosure::describe` with
//                         ROW BLOBS and an UNBOUNDED inline budget, in the
//                         clipboard envelope's shape.
//
// The payload is the clipboard's envelope on purpose. That format already
// carries exactly what a bundle needs — the catalog row (name, type,
// placement, `asset` blob, properties), every file with its role and its
// content id, and the BYTES inline — and `ClipboardResolver::apply` is
// already the import that lands one: ingest by content, register the rows,
// write the intrinsic edges, pin into the open project. Writing a second
// importer for the same payload would be a second set of rules for one
// question.
//
// NOT the legacy .jaf. That format is a database blob plus a directory of
// files named by DISPLAY name, and the material half of it
// (`Exporter::exportShaderAsMaterial`) minted a Material row at export time
// and copied by name — which is why it is deleted with this lane.
//
// WHAT TRAVELS: the asset, its members (the textures it names, the maps its
// bake produced), their bytes and their rows. What does NOT travel is
// anything reserved — the builtins exist in every install.

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

#include "io/clipboardformat.h"
#include "services/bundlewriter.h"

class Database;
class Project;

namespace assetshare {

/// The share file's extension, without the dot.
inline const char *extension() { return "jbundle"; }

/// The file-dialog filter both the module and the Assets page use.
QString fileFilter();

/// STAGE `guids` and everything they are made of as a share file: the rows,
/// edges and store paths, read on the calling (UI) thread; the bytes are owed
/// and read by the writer (services/bundlewriter.h), on a worker. ONE entry's
/// manifest kind is its type word ("material", "texture", "sky"); a SET is a
/// "pack" (the Assets tray's multi-selection). The version that travels is the
/// one the OPEN PROJECT renders with when it holds an asset (its pin), the
/// library's otherwise — copy semantics, the same rule the clipboard uses.
///
/// `yield` (optional) runs between slices of the catalog reads — the caller's
/// event-loop turn (yieldToEventLoop), so a large set stages without one long
/// block on the UI thread.
BundleStage stageBundle(Database *db, Project *project, const QStringList &guids,
                        const std::function<void()> &yield = {});

/// A SCENE NODE, staged (ARCHIVE-ROUNDTRIP, D7) — the file behind the
/// outliner's Export Object / Export Particle System and node.exportArchive.
/// The same format as stageBundle: the node becomes the row the file is about
/// — an Object (`typeId`) row, fresh guid, its `asset` blob the node — with the
/// node's whole closure and every byte beside it; `assets.import` of the file
/// lands them all and answers that row, which `assets.addToScene` instantiates.
BundleStage stageNode(Database *db, Project *project, const QJsonObject &nodeObject,
                      const QString &name, int typeId, const std::function<void()> &yield = {});

/// One turn of the calling thread's event loop with user input held back — the
/// `yield` every export door hands the stage.
void yieldToEventLoop();

/// THE VERBS' EXPORT (assets.exportBundle): stageBundle, then the writer on a
/// worker, waited for with the event loop turning (bundlewriter.h,
/// exportAndWait). The UI doors stage and hand the job to
/// ui/dialogs/bundleexportdialog.h instead.
ExportResult exportBundle(Database *db, Project *project, const QStringList &guids,
                          const QString &destPath);

/// node.exportArchive: stageNode, then the writer on a worker (as above).
ExportResult exportNode(Database *db, Project *project, const QJsonObject &nodeObject,
                        const QString &name, int typeId, const QString &destPath);

struct ImportResult
{
    QString guid;            ///< the asset the file is ABOUT (a pack's first entry)
    QStringList guids;       ///< every entry the file is about, in its order
    QStringList imported;    ///< the rows this import created
    QStringList known;       ///< the rows this library already had
    QString error;
    /// TRUE when the library ALREADY held the asset the file is about, so
    /// nothing about it changed: the import pinned the version that is here
    /// rather than the version in the file. Said out loud because the
    /// gesture otherwise looks like a successful update and is not one —
    /// "update an asset I already have from a share file" is a decision
    /// nobody has taken yet (there is no merge rule, and overwriting a row
    /// other projects pin is the opposite of the pin law).
    bool alreadyHad = false;
    bool ok() const { return error.isEmpty() && !guid.isEmpty(); }
};


/// Land a share file in this library (and pin it into the open project, if
/// there is one). A guid this library already holds is NOT overwritten — the
/// same content answers the same asset, and different content under a guid
/// that is taken is remapped by the resolver.
ImportResult importBundle(Database *db, Project *project, const QString &path);

/// Does this path look like a share file? Extension first (cheap), then the
/// manifest inside it — a zip with our manifest is one whatever it is called.
bool looksLikeBundle(const QString &path);

}   // namespace assetshare

#endif   // ASSETSHARE_H
