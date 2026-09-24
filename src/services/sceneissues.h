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

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

#include "irisgl/irisglfwd.h"

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

    /// Forgets every live issue of a kind. Returns how many went.
    int clearKind(const QString &kind);

    /// Forgets everything — a scene close, or a test between cases.
    void reset();

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
    ///   * `sky.duplicate` — a second Sky Light, which lights nothing;
    ///   * `rays.absent` — the project's Ray Tracing row says ON (it
    ///     was AUTHORED for rays) and this machine has none. The one issue that
    ///     is about the PROJECT rather than an object, so it names no node —
    ///     and the entire reason the On state exists, since On and Auto render
    ///     the same picture and differ only in whether the author is told.
    ///   * `texture.missing` — a material's texture file is gone from disk
    ///     (the document keeps the path; the mirror binds nothing). One issue
    ///     per mesh node, naming each missing slot and file; it clears when
    ///     the file returns or the slot is re-linked.
    int scan(const iris::ScenePtr &scene);

signals:
    /// Anything changed: raised, cleared, reset.
    void changed();

private:
    SceneIssues() = default;
    int indexOf(const QString &id) const;

    QVector<SceneIssue> mIssues;
};

#endif // SCENEISSUES_H
