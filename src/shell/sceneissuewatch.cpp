/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "shell/sceneissuewatch.h"

#include <QTimer>
#include <QWidget>

#include "data/database/database.h"
#include "services/sceneeditservice.h"
#include "services/sceneissues.h"
#include "ui/controls/sceneissuebar.h"
#include "viewport/ieditorviewport.h"

SceneIssueWatch::SceneIssueWatch(QWidget *window, std::function<bool()> editorActive,
                                 QObject *parent)
    : QObject(parent), mWindow(window), mEditorActive(std::move(editorActive))
{
}

void SceneIssueWatch::setScene(SceneEditService *sceneEdit, IEditorViewport *viewport)
{
    mSceneEdit = sceneEdit;
    mViewport = viewport;
}

void SceneIssueWatch::start()
{
    if (mTimer) return;
    mTimer = new QTimer(this);
    mTimer->setInterval(1000);
    connect(mTimer, &QTimer::timeout, this, [this]() { update(); });
    mTimer->start();

    // THE LIBRARY ITSELF CAN FAIL, AND THE USER HAS TO BE TOLD (CLOSE-2 round
    // 2, H7). A gesture's database writes ride one transaction now, so a
    // commit that fails rolls back EVERYTHING that gesture wrote — a whole
    // script run's asset rows — and until this line existed the only trace was
    // a warn in the log, which has an audience of one. It is a scene-issue and
    // not a toast for the reason the bar exists: it stays up until the
    // condition is gone, and the condition going away is the very next gesture
    // committing. No node to select; the action is the only thing to say.
    Database::setBatchCommitListener([](bool ok) {
        const QString id = QStringLiteral("library.write");
        if (ok) { SceneIssues::instance().clear(id); return; }
        SceneIssue issue;
        issue.id = id;
        issue.kind = QStringLiteral("library.write");
        issue.message = tr("The library could not be saved, so the changes from the last "
                           "action were not kept.");
        issue.action = tr("Check that the disk is not full and that the library file is not "
                          "read-only, then try the action again.");
        SceneIssues::instance().raise(issue);
    });
}

// ONE PASS: scan the open scene, and decide whether the bar may be on screen.
// Driven by the 1 Hz timer and by every space switch.
void SceneIssueWatch::update()
{
    // THE BAR IS AN EDITOR SURFACE, and it is a FRAMELESS TOP-LEVEL WITH
    // WindowStaysOnTopHint (sceneissuebar.cpp) — so without this check it
    // floated over the Desktop, Assets, Player and Materials pages, describing
    // a scene nobody is looking at (item 3).
    if (!mEditorActive || !mEditorActive()) {
        if (mBar) mBar->setEditorActive(false);
        return;
    }
    if (mBar) mBar->setEditorActive(true);
    if (!mSceneEdit) return;
    auto scene = mSceneEdit->scene();
    if (!scene) { SceneIssues::instance().reset(); return; }
    SceneIssues::instance().scan(scene);
    if (!mBar && SceneIssues::instance().count() > 0) {
        mBar = new SceneIssueBar(mWindow);
        // Under the engine-drawn frame-stats rows (three lines plus their
        // inset) so the two never overlap when F3 is on.
        mBar->setAnchor(mViewport ? mViewport->asWidget() : nullptr, 96);
        mBar->refresh();
    }
}

// What the bar is showing, for `editor.issueBar()` — the seam the shell's half
// of the error area is tested through (item 3's case in
// scripting.e2e.scene_issues).
QVariantMap SceneIssueWatch::state() const
{
    QVariantMap out;
    out[QStringLiteral("editorActive")] = mEditorActive && mEditorActive();
    out[QStringLiteral("exists")] = !mBar.isNull();
    out[QStringLiteral("visible")] = mBar && mBar->isVisible();
    out[QStringLiteral("rows")] = SceneIssues::instance().count();
    // What is actually BUILT: one line per issue (plus the "+N more" line), and
    // no clickable control anywhere in it. `buttons` is asserted to be zero by
    // scripting.e2e.scene_issues — the owner's "just show the error" rule, in a
    // form that cannot rot.
    out[QStringLiteral("lines")] = mBar ? mBar->lineCount() : 0;
    out[QStringLiteral("buttons")] = mBar ? mBar->buttonCount() : 0;
    return out;
}
