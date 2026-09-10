/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLIPBOARDRESOLVER_H
#define CLIPBOARDRESOLVER_H

// ClipboardResolver — a pasted guid this library has never seen
// (CLIPBOARD_SPEC §3.4). The hard half of "deep": the document data always
// travels, but a mesh, a texture or an avatar definition is a REFERENCE, and
// a reference is only as good as the resolution behind it.
//
// FIVE STEPS, in this order, per referenced guid:
//
//   1. KNOWN HERE (the catalog holds the guid, at the right TYPE — the
//      reserved builtin ids deliberately collide across types, so an untyped
//      check resolves a pasted primitive to a shader). A paste into a
//      different project of the same library PINS it as a BINDING: a paste is
//      a dependency, exactly like a light's IES profile, never a Direct add
//      (which would mint a companion material nobody asked for).
//   2. UNKNOWN, BYTES INLINE — stage, ingest CAS-first UNDER THE ORIGINAL
//      GUID, register the row, pin. Identity is preserved because an asset
//      guid IS the identity (ASSET_PIPELINE I1): the same texture pasted into
//      five libraries is one asset, not five.
//   3. UNKNOWN, OID ONLY, and the LOCAL store already has that object —
//      content dedup means another library on this machine may hold the exact
//      bytes. Register over the existing object; no copy.
//   4. UNKNOWN, OID ONLY, and the SOURCE store root is readable here and its
//      store.json id matches — ingest from `<root>/objects/aa/<oid>.<ext>`
//      (a hardlink when the filesystem allows, which AssetCas::storeObject
//      already does).
//   5. MISSING. Reported, with what needs it. By DEFAULT the items that need
//      it are refused rather than pasted: the scene reader's missing-asset
//      behaviour is silent (scenereader.cpp:1127-1145), so an "Unreal-style"
//      paste with nulls yields an invisible node with no log — a bug report,
//      not a feature. `allowMissing` opts into it explicitly.
//
// DELIBERATE DEVIATION from the spec's §3.4 step 2 wording, recorded here
// because it is the one place this lane did not do what the spec said: the
// resolver REGISTERS THE ROW ITSELF (ingest + createAssetEntry +
// createDependency + sidecar, exactly the import spine's commit shape) rather
// than routing inline bytes back through AssetImportService with a new
// "import under this guid" flag. Three reasons, and §6.5 of the same spec
// already says the second one: (a) the envelope carries the catalog row's own
// columns — type, parent, view filter, blob, properties, dependency edges —
// so re-deriving them by re-importing the bytes would LOSE information the
// payload has; (b) the spec forbids the archive path's textual guid rewrite
// for clipboard content, and the import spine's .jaf route is that path; (c)
// the import spine mints guids by construction, and a flag that says "except
// this time" is a fork of the one pipeline every other import shares.

#include <QHash>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVector>

#include "io/clipboardformat.h"

class Database;
class Project;

/// An asset the paste needs and cannot get.
struct ClipboardMissing
{
    QString guid;
    QString name;
    QString type;
    qint64 size = -1;
    QString neededBy;    ///< the item (and key) that referenced it
};

struct ClipboardResolveReport
{
    QStringList known;        ///< already in this catalog
    QStringList importable;   ///< resolvable (inline bytes, or an oid we can find)
    QStringList imported;     ///< actually registered (apply only)
    QStringList pinned;       ///< pinned into the target project (apply only)
    QVector<ClipboardMissing> missing;
    /// envelope guid -> local guid, for the rare TYPE COLLISION (a guid this
    /// catalog holds as a different type). Applied to the items by the same
    /// key-aware walk that computed the closure — never a textual replace.
    QHash<QString, QString> guidMap;
    QString error;

    bool complete() const { return missing.isEmpty(); }
};

class ClipboardResolver
{
public:
    ClipboardResolver(Database *db, Project *project);

    /// Dry run: what a paste would find, import or miss. No writes — no rows,
    /// no pins, and no staged bytes either (`plan` never touches the disk).
    ///
    /// `limitTo`, when given, restricts the walk to those guids: a paste
    /// resolves only what the items it is ACTUALLY GOING TO LAND need. Without
    /// it a paste imported and pinned the whole payload's closure before it had
    /// decided anything — so a node paste aimed at the Assets page still pinned
    /// every texture, and an item refused for a missing mesh still dragged its
    /// other assets into the project (spec §3.1, "nothing partial happens").
    ClipboardResolveReport plan(const clipboardformat::Envelope &envelope,
                                const QSet<QString> *limitTo = nullptr) const;

    /// Steps 1-4 for real: ingest, register, pin. Idempotent (CAS dedup, pin
    /// upsert), which is what lets it sit OUTSIDE the paste's undo macro.
    ClipboardResolveReport apply(const clipboardformat::Envelope &envelope,
                                 const QSet<QString> *limitTo = nullptr);

private:
    ClipboardResolveReport run(const clipboardformat::Envelope &envelope, bool commit,
                               const QSet<QString> *limitTo) const;
    /// Registers one asset (bytes already located at `sourcePath`).
    bool registerAsset(const clipboardformat::ClipAsset &asset,
                       const QVector<QPair<clipboardformat::ClipFile, QString>> &files,
                       QString *errorOut) const;

    Database *db;
    Project *project;
};

#endif // CLIPBOARDRESOLVER_H
