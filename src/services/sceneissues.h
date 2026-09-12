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
//   3. is dismissible, and never repeats itself while the same condition holds.
//
// WHAT "NEVER REPEATS" MEANS, precisely, because it is the part that is easy to
// get wrong: an issue has a stable `id` built from its kind and its subject.
// Raising an id that is already live is a NO-OP — no signal, no second row, no
// re-notify. Dismissing it hides it but keeps it live, so a scanner that keeps
// finding the same condition sixty times a second cannot bring it back. Only
// `clear()` — the condition genuinely going away — forgets it, and the next
// occurrence is then a new event the user sees again.
//
// This is NOT the transient `Toast` (ui/dialogs/toast.h): a toast is a message
// that appears and leaves (snap size, "capture saved"). A scene issue stays up
// until the scene is fixed or the user waves it away, which is why it has its
// own store, its own verbs and its own widget.
//
// API-FIRST (SCRIPTING_SPEC §2.3): the store is the model, `editor.issues` /
// `editor.raiseIssue` / `editor.dismissIssue` / `editor.checkScene` are the
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
    bool    dismissed = false;

    QVariantMap toMap() const;
};

class SceneIssues : public QObject
{
    Q_OBJECT

public:
    static SceneIssues &instance();

    /// Raises an issue, or does NOTHING when one with the same id is already
    /// live (dismissed or not). Returns the id either way, and `raised` says
    /// which of the two happened.
    QString raise(const SceneIssue &issue, bool *raised = nullptr);

    /// The user waved it away. Stays live (so the scanner cannot re-raise it),
    /// stops being shown. False when there is no such issue.
    bool dismiss(const QString &id);

    /// The condition is gone. Forgets it, so the NEXT occurrence is shown
    /// again. False when there is no such issue.
    bool clear(const QString &id);

    /// Forgets every live issue of a kind. Returns how many went.
    int clearKind(const QString &kind);

    /// Forgets everything — a scene close, or a test between cases.
    void reset();

    /// Live issues, oldest first. `includeDismissed` false is what the error
    /// bar shows; true is what `editor.issues()` reports.
    QVector<SceneIssue> issues(bool includeDismissed = true) const;
    QVariantList toVariant(bool includeDismissed = true) const;

    /// How many issues the user can currently see.
    int visibleCount() const;

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
    ///     Showroom's lamp above a sealed roof, which nothing ever mentioned).
    int scan(const iris::ScenePtr &scene);

signals:
    /// Anything changed: raised, dismissed, cleared, reset.
    void changed();

private:
    SceneIssues() = default;
    int indexOf(const QString &id) const;

    QVector<SceneIssue> mIssues;
};

#endif // SCENEISSUES_H
