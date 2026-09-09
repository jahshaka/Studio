/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef CLIPBOARDSERVICE_H
#define CLIPBOARDSERVICE_H

// ClipboardService — THE clipboard (SPECS/CLIPBOARD_SPEC.md P0+P1).
//
// ONE clipboard for the whole application, and it is the SYSTEM clipboard
// (decision D3 b). Before this, three existed: an in-app QList<SceneFragment>
// on SceneEditService for the editor, the system clipboard for the Materials
// graph, and nothing at all for assets — so "which clipboard am I pasting
// from" was a real question with a per-space answer. The in-app list is gone
// (its copyNodes/paste/clipboard trio deleted, CRUD law); what remains is one
// service that writes a versioned TEXT envelope (src/io/clipboardformat.h)
// under both `application/x-jahshaka-clipboard` and `text/plain`, so a copy
// crosses to a second instance, to a text editor, to a chat window, and back.
//
// WHAT "DEEP" MEANS HERE. A copied selection carries its document data inline
// (that IS the copy) and its ASSET CLOSURE by content id — with the bytes
// inline under a budget (`clipboard/inlineLimitBytes`, 4 MB by default). A
// paste into another project of the same library PINS what it needs; a paste
// into a library that has never seen the asset IMPORTS it from the inline
// bytes, under the guid it came with, because an asset guid is identity
// (ASSET_PIPELINE invariant I1). What cannot be resolved is REPORTED, and by
// default the items needing it are refused rather than pasted as invisible
// nodes with no mesh (§3.4 D5).
//
// SEAMS. The BACKEND is an interface (clipboardbackend.h) so the codec and the
// paste path are testable with no QClipboard at all; the RESOLVER
// (clipboardresolver.h) owns cross-library resolution; the CLOSURE
// (assetclosure.h) owns "what has to travel". This class owns the ORDER those
// three run in, the undo macro, and the selection that results.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include "irisgl/irisglfwd.h"

#include "io/clipboardformat.h"
#include "services/clipboardbackend.h"
#include "services/clipboardresolver.h"

class Database;
class Project;
class SceneEditService;
class SelectionService;
class UndoService;
class SettingsManager;

/// What a copy put on the clipboard.
struct ClipboardCopyResult
{
    int items = 0;
    int assets = 0;         // entries in the closure
    int inlined = 0;        // assets whose bytes travelled
    int referenced = 0;     // assets carried by oid only (over the budget)
    qint64 bytes = 0;       // the payload's size in bytes, as written
    QString error;          // non-empty = nothing was copied
    bool ok() const { return error.isEmpty() && items > 0; }
};

/// Where a paste lands. `target` is the SURFACE asking (D10): one verb,
/// dispatched, so every UI surface is a caller rather than a second paste.
struct ClipboardPasteOptions
{
    QString target = QStringLiteral("tree");   // "tree" | "assets"
    QString parentGuid;                        // tree: explicit parent (empty = beside the primary)
    int index = -1;                            // tree: sibling index (-1 = the D7 rule)
    bool allowMissing = false;                 // §3.4 step 5: paste with placeholders
};

struct ClipboardSkip
{
    QString kind;
    QString reason;
};

struct ClipboardPasteResult
{
    QStringList pasted;                        // new node guids, in paste order
    QStringList imported;                      // asset guids registered here
    QStringList pinned;                        // asset guids pinned into this project
    QVector<ClipboardMissing> missing;
    QVector<ClipboardSkip> skipped;
    QString error;
    bool ok() const { return error.isEmpty(); }
};

/// What a cut did: the copy it published and the delete it performed.
struct ClipboardCutResult
{
    ClipboardCopyResult copy;
    QStringList removed;     ///< node guids that left the document
    QStringList skipped;     ///< members the delete refused (World root, non-removable)
    QString error;
    bool ok() const { return error.isEmpty() && copy.ok(); }
};

class ClipboardService : public QObject
{
    Q_OBJECT
public:
    /// `backend` may be null — a SystemClipboardBackend is made then. The
    /// service takes ownership of whatever it ends up holding.
    ClipboardService(Database *db, Project *project, SceneEditService *sceneEdit,
                     SelectionService *selection, UndoService *undo,
                     ClipboardBackend *backend = nullptr, QObject *parent = nullptr);
    ~ClipboardService() override;

    /// The inline budget, in bytes (`clipboard/inlineLimitBytes`, default
    /// 4 MB). Read from the settings on every copy so a change takes effect
    /// without a restart.
    qint64 inlineLimitBytes() const;
    void setInlineLimitBytes(qint64 bytes);

    // ---- copy ------------------------------------------------------------

    /// The effective set (D5-reduced, World root out) as `node` items, with
    /// the asset closure. NOT an undo entry.
    ClipboardCopyResult copyNodes(const QList<iris::SceneNodePtr> &nodes);

    /// Library tiles as `asset` items — the closure of each, plus its catalog
    /// row (name/type/blob/properties) so a paste into a library that has
    /// never seen the guid can register it.
    ClipboardCopyResult copyAssets(const QStringList &guids);

    // ---- paste -----------------------------------------------------------

    /// §3: resolve the closure, then land the items. ONE undo macro for every
    /// document mutation; asset ingest and pins sit OUTSIDE it (D7 — asset
    /// operations have no undo anywhere and are idempotent).
    ClipboardPasteResult paste(const ClipboardPasteOptions &options = ClipboardPasteOptions());

    /// Copy, then delete what was copied. The COPY is not undoable (nothing
    /// in the document changed); the delete is the shipped set delete, one
    /// undo macro — so Ctrl+Z after a cut brings the objects back and the
    /// clipboard still holds them.
    ClipboardCutResult cutNodes(const QList<iris::SceneNodePtr> &nodes);

    // ---- reading ---------------------------------------------------------

    /// The parsed envelope on the clipboard right now, or a null envelope when
    /// it holds something else. Cached: the service remembers the BYTES it
    /// last wrote or parsed and skips re-parsing while the clipboard still
    /// holds them, so an in-app paste costs nothing.
    clipboardformat::Envelope contents() const;

    /// The raw payload (Unreal's `SourceData`) and the way a script or a test
    /// puts one on the clipboard without a copy. `setText` VALIDATES and
    /// reports; it never pastes.
    QByteArray text() const;
    bool setText(const QByteArray &payload, QString *errorOut = nullptr);

    /// Dry run of §3.4 — what a paste would find, import or miss.
    ClipboardResolveReport resolve() const;

    /// True when the payload came from THIS library (store id match).
    bool sameLibrary(const clipboardformat::Envelope &envelope) const;

    /// The store's identity and root, as the envelope records them.
    QString storeId() const;

signals:
    /// A paste registered library assets — the Assets page should re-list.
    void assetsImported(const QStringList &guids);

private:
    iris::ScenePtr scene() const;
    clipboardformat::Envelope newEnvelope() const;
    /// Writes the payload and refreshes the cache.
    ClipboardCopyResult publish(const clipboardformat::Envelope &envelope, int items);

    Database *db;
    Project *project;
    SceneEditService *sceneEdit;
    SelectionService *selection;
    UndoService *undo;
    ClipboardBackend *backend;

    /// The LAST PAYLOAD WE SAW (D3 b): bytes plus their parse. The clipboard
    /// is the truth; this only avoids re-parsing it.
    mutable QByteArray cachedText;
    mutable clipboardformat::Envelope cachedEnvelope;
};

#endif // CLIPBOARDSERVICE_H
