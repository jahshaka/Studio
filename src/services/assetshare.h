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

#include <QString>
#include <QStringList>

class Database;
class Project;

namespace assetshare {

/// The share file's extension, without the dot. A format of its own, not
/// `.jaf`: the legacy importer sniffs that one and would take this file to
/// pieces looking for an `asset.db` that is not there.
inline const char *extension() { return "jbundle"; }

/// The file-dialog filter both the module and the Assets page use.
QString fileFilter();

struct ExportResult
{
    QString path;      ///< the archive written
    QString kind;      ///< the manifest's kind ("material", "texture", …)
    int assets = 0;    ///< rows carried (the asset plus its closure)
    qint64 bytes = 0;  ///< the archive's size
    QString error;
    bool ok() const { return error.isEmpty() && !path.isEmpty(); }
};

/// `guid` and everything it is made of, as a share file at `destPath`. The
/// version that travels is the one the OPEN PROJECT renders with when it
/// holds the asset (its pin), the library's otherwise — copy semantics, the
/// same rule the clipboard uses.
ExportResult exportBundle(Database *db, Project *project, const QString &guid,
                          const QString &destPath);

struct ImportResult
{
    QString guid;            ///< the asset the file is ABOUT
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
