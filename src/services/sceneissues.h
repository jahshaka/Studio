/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEISSUES_H
#define SCENEISSUES_H

// SceneIssues — the USER-FACING error area for problems in the open scene.
// (SUN_AND_LIGHT_DEFAULTS_SPEC owner decisions Q1b/Q1c.)
//
// THE RULE, and it is the whole design (owner, 2026-09-13): *if the person
// using the editor can fix it in their scene, it is a toast; if it exists for
// us to debug the engine, it stays in the log.* Everything in here is the first
// half. Shader compiles, cache misses, pass counts, warm-up chatter, the
// "[open-profile] slow frame ... shader/PSO compilation is the usual cause"
// line — all of that has an audience of one (the lead), and it belongs to
// jahlog and the monitor's capture bundle. None of it may be raised here.
//
// An issue therefore ALWAYS:
//   1. names the thing in the scene (`node` — a guid the UI can select), so the
//      user can go and look at it;
//   2. says what to DO about it in plain words (`action`), not what went wrong
//      internally;
//   3. never repeats itself while the same condition holds, and goes away by
//      ITSELF when the user has fixed it.
//
// NOTHING HERE IS DISMISSIBLE (owner, 2026-09-13: "get rid of the Select
// button and the button next to it so it just shows the error, let the user fix
// it"). A message the user can wave away is a message they have to act on
// twice, and an error area with a close button becomes a thing people close
// instead of a thing people read. The ONLY way a line leaves the bar is the 1 Hz
// scanner finding the condition gone — which is the same thing as the scene
// being right again.
//
// WHAT "NEVER REPEATS" MEANS, precisely, because it is the part that is easy to
// get wrong: an issue has a stable `id` built from its kind and its subject.
// Raising an id that is already live is a NO-OP — no signal, no second row, no
// re-notify. Only `clear()` — the condition genuinely going away — forgets it,
// and the next occurrence is then a new event the user sees again.
//
// This is NOT the transient `Toast` (ui/dialogs/toast.h): a toast is a message
// that appears and leaves (snap size, "capture saved"). A scene issue stays up
// until the scene is fixed, which is why it has its own store, its own verbs
// and its own widget.
//
// EVERY LIVE ISSUE IS SHOWN, LINE BY LINE (owner, same conversation: "it should
// just list all errors in the scene line by line if there are multiple"), in a
// STABLE ORDER — by kind, then by the object it is about — so a second issue
// appearing never reshuffles the line the user was reading. `issues()` sorts;
// the store's insertion order is an implementation detail nobody may depend on.
//
// API-FIRST (SCRIPTING_SPEC §2.3): the store is the model, `editor.issues` /
// `editor.raiseIssue` / `editor.clearIssue` / `editor.checkScene` are the
// verbs, and the viewport's error bar is a view of exactly this and nothing
// else. Process-wide, like the other message sinks (EngineErrorPump), because
// there is one editor window and one open scene.

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWeakPointer>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "irisgl/core/math/vec.h"
#include "irisgl/document/scenegraph/nodegraph.h"
#include "irisgl/irisglfwd.h"

#include "io/missingclipref.h"

class QFileSystemWatcher;

/// One thing wrong with the open scene that the user can fix.
struct SceneIssue
{
    QString id;         ///< stable: "<kind>:<node>" (or just "<kind>")
    QString kind;       ///< machine-readable class, e.g. "sun.tie"
    QString node;       ///< guid of the object to select; empty = nothing to select
    QString nodeName;   ///< its name at the time it was raised, for the message
    QString message;    ///< what is wrong, in the user's words
    QString action;     ///< what to do about it, in the user's words
    qint64  raisedMs = 0;

    QVariantMap toMap() const;
};

class SceneIssues : public QObject
{
    Q_OBJECT

public:
    static SceneIssues &instance();

    /// Raises an issue, or does NOTHING when one with the same id is already
    /// live. Returns the id either way, and `raised` says which of the two
    /// happened.
    QString raise(const SceneIssue &issue, bool *raised = nullptr);

    /// The condition is gone. Forgets it, so the NEXT occurrence is shown
    /// again. False when there is no such issue.
    bool clear(const QString &id);
    /// The SAME condition, described afresh: replaces a live issue's message
    /// IN PLACE — no move, no second row, and a `changed()` only when the text
    /// really differs. For an issue whose wording tracks its subject (the
    /// missing texture files of one object). False when there is no such issue.
    bool update(const QString &id, const QString &message);

    /// Forgets every live issue of a kind. Returns how many went.
    int clearKind(const QString &kind);

    /// Forgets everything — a scene close, or a test between cases.
    void reset();

    /// One `model.missing` issue per model file (FORWARD-ONLY-1): a model with
    /// no current bake that could not be rebuilt from its source — the open's
    /// SceneReader::missingModels(), and a library placement's.
    void raiseMissingModels(const QStringList &paths);
    /// One `clip.missing` per skeletal clip a reader could not resolve
    /// (SceneReader::missingClips()): which clip, which asset, and why.
    void raiseMissingClips(const QVector<MissingClipRef> &clips);
    /// `gi.cascades.clamped`: the open scene pinned more cascade rows than the renderer's
    /// slot table holds; the reader kept the first kGiTierMaxCascades and dropped `dropped`.
    void raiseCascadeSetClamped(int dropped);

    /// Every live issue, in the STABLE order the bar lists them in: by kind,
    /// then by the object each is about, then by id. There is no second,
    /// smaller list — everything live is shown.
    QVector<SceneIssue> issues() const;
    QVariantList toVariant() const;

    /// How many issues the user can currently see (= issues().size()).
    int count() const;

    /// THE SCANNER. Walks the open scene for the conditions we know how to
    /// describe, raising what it finds and clearing what has been fixed.
    /// Idempotent by construction (see the "never repeats" note above), so it
    /// is safe to call on a timer, and it is the seam a test drives.
    /// Returns how many issues are live afterwards.
    ///
    /// The conditions, both owner-reported:
    ///   * `sun.tie` — two or more directional lights claim the same forward
    ///     shading priority, so which one is the sun is decided by a tie-break
    ///     the author never chose;
    ///   * `shadow.leak` — a light whose shadows are switched off stands close
    ///     enough to opaque geometry to light straight through it (the
    ///     Showroom's lamp above a sealed roof, which nothing ever mentioned);
    ///   * `sky.duplicate` — a second Sky Light, which does nothing;
    ///   * `rays.absent` — the project's Ray Tracing row says ON (it
    ///     was AUTHORED for rays) and this machine has none. The one issue that
    ///     is about the PROJECT rather than an object, so it names no node —
    ///     and the entire reason the On state exists, since On and Auto render
    ///     the same picture and differ only in whether the author is told.
    ///   * `texture.missing` — a material's texture file is gone from disk
    ///     (the document keeps the path; the mirror binds nothing). One issue
    ///     per mesh node, naming each missing slot and file; it clears when
    ///     the file returns or the slot is re-linked.
    ///
    /// THE NODE CHECKS FOLLOW THE DOCUMENT'S OWN DIRT, NEVER THE CLOCK
    /// (BISTRO-ENGINE-1 stage 5, finding 14). The two node conditions —
    /// `shadow.leak` (each solid node's world box) and `texture.missing` (each
    /// mesh's material) — used to walk the whole scene on every call, and the
    /// editor calls this once a second: ~0.2 s of UI thread per second at
    /// 6,000 meshes in a Debug build (the owner's Bistro session, 218 heartbeat
    /// lines of ~430 ms). Each node's answer is now CACHED, and a pass
    /// re-checks only the nodes the document says were written since the last
    /// pass — its own cursor on the node graph's write journal (graph::
    /// WriteJournalReader with the written half: a moved node with its subtree,
    /// a written node alone) — plus the nodes
    /// whose material's revision moved or whose texture file the disk
    /// reported. No write = no node visited. A whole walk only for a new
    /// scene, or when the journal cannot name what was written.
    /// The light-list conditions (`sun.tie`, `sky.duplicate`) and the machine
    /// ones (`rays.absent`, `vr.colour`) are O(lights) and run every pass.
    int scan(const iris::ScenePtr &scene);

    /// What the LAST scan() did — the seam the walk gating is tested through
    /// (scripting.e2e.scene_issue_idle): the pass number, whether each walk
    /// ran, and how many nodes the pass visited (0 for an idle pass).
    struct ScanStats
    {
        int passes = 0;          ///< scan() calls with a scene, ever
        int walks = 0;           ///< passes that visited any node, ever
        bool fullWalk = false;   ///< the last pass walked the whole scene
        bool leakEvaluated = false;   ///< the last pass re-tested the lamps against the boxes
        int nodesVisited = 0;    ///< nodes the last pass re-checked
        qint64 visitedTotal = 0; ///< nodes re-checked by every pass, ever
    };
    ScanStats lastScan() const { return mStats; }

signals:
    /// Anything changed: raised, cleared, reset.
    void changed();

private:
    SceneIssues() = default;
    int indexOf(const QString &id) const;
    /// texture.missing's view of the disk (see the scanner): whether each
    /// texture path exists, as last asked; forgotten per directory when the
    /// watcher reports it changed, and wholesale every kTextureRecheckScans.
    bool textureExists(const QString &path);
    void forgetTextureDir(const QString &dir);
    /// The minute's backstop (see kTextureRecheckScans): re-asks the disk
    /// about every path already cached ON A WORKER, and hands the answers
    /// that flipped back to the UI thread (mFlippedPaths).
    void startTextureRestat();
    QHash<QString, bool> mTextureExists;
    QFileSystemWatcher *mTextureDirs = nullptr;
    int mScansSinceRecheck = 0;
    bool mRestatRunning = false;
    /// Paths whose answer the disk may have changed since the last pass.
    QSet<QString> mFlippedPaths;

    /// ONE NODE'S CACHED ANSWERS. Keyed by the node's address, held weakly:
    /// a dead or departed node is dropped, never followed.
    struct NodeCheck
    {
        QWeakPointer<iris::SceneNode> node;
        bool blocker = false;          ///< visible, casts a shadow, has a box
        iris::Vec3 mn, mx;             ///< its world box (blockers only)
        QWeakPointer<iris::Material> material;
        quint32 materialRevision = 0;
        QStringList paths;             ///< the texture files the check asked about
        SceneIssue texture;            ///< its texture.missing line (id empty = none)
    };
    QHash<const iris::SceneNode *, NodeCheck> mChecks;
    QWeakPointer<iris::Scene> mCheckedScene;
    /// This checker's cursor on the write journal, with its WRITTEN half (every non-transform document write) —
    /// one reader among the mirrors and the physics worlds, never a second record (ENGINE-UPGRADE-1's one journal).
    iris::graph::WriteJournalReader mJournal{ true };
    quint64 mMaterialsSeen = 0;
    bool mLeakDirty = true;
    QHash<QString, SceneIssue> mLeakLines;   ///< by issue id
    ScanStats mStats;
    /// Re-derives one node's cached answers (and raises/updates its line).
    void checkNode(iris::SceneNode *node);
    /// Drops every node of `node`'s subtree from the cache.
    void forgetSubtree(iris::SceneNode *node);
    /// Forget every cached answer: the next pass walks the whole scene.
    void forgetWalks();
    QVector<SceneIssue> mIssues;
};

#endif // SCENEISSUES_H
