/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_EDITORAPI_H
#define SCRIPTING_EDITORAPI_H

// editor.* — selection, undo, play, deterministic frames, screenshots
// (SCRIPTING_SPEC §1.2). screenshot + frame are the verification primitives:
// they work headless (offscreen View + readPixels, no window grab).

#include <QList>
#include <QVariantMap>

#include "irisgl/irisglfwd.h"

#include "scripting/apimodule.h"

class EditorApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("editor"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE bool select(const QVariant &id = QVariant());
    Q_INVOKABLE QVariant selection();
    Q_INVOKABLE QVariantList selectionSet();
    Q_INVOKABLE bool selectAdd(const QVariant &id = QVariant());
    Q_INVOKABLE bool selectToggle(const QString &id);
    Q_INVOKABLE QVariantList selectRange(const QString &fromId, const QString &toId);
    Q_INVOKABLE bool selectNone();
    Q_INVOKABLE QVariantMap deleteSelection();
    Q_INVOKABLE QVariantList duplicateSelection();
    Q_INVOKABLE int copy();
    Q_INVOKABLE QVariantList paste();
    Q_INVOKABLE QVariantList clipboard();
    Q_INVOKABLE QString gizmoMode();
    Q_INVOKABLE bool setGizmoMode(const QString &mode);
    Q_INVOKABLE bool focusSelection();
    Q_INVOKABLE bool gameView(bool enabled);
    Q_INVOKABLE bool isGameView();
    Q_INVOKABLE QVariantMap overlays();
    Q_INVOKABLE bool setOverlays(const QVariantMap &change = QVariantMap());
    Q_INVOKABLE bool setView(const QString &view);
    Q_INVOKABLE QString view();
    Q_INVOKABLE QVariantMap camera();
    Q_INVOKABLE QVariantMap setCamera(const QVariant &pose);
    Q_INVOKABLE QVariantMap frameNode(const QString &id, const QVariant &options = QVariant());
    Q_INVOKABLE bool pilot(const QVariant &id = QVariant());
    Q_INVOKABLE QVariant piloting();
    Q_INVOKABLE bool setViewCamera(const QVariant &id = QVariant());
    Q_INVOKABLE QVariantMap pip();
    Q_INVOKABLE QVariantMap setPip(const QVariantMap &change = QVariantMap());
    Q_INVOKABLE QVariantMap flySpeed();
    Q_INVOKABLE QVariantMap setFlySpeed(const QVariant &multiplier);
    Q_INVOKABLE QString cameraMode();
    Q_INVOKABLE bool setCameraMode(const QString &mode);
    Q_INVOKABLE QString gizmoSpace();
    Q_INVOKABLE bool setGizmoSpace(const QString &space);
    Q_INVOKABLE bool fullscreen(const QVariant &on = QVariant());
    Q_INVOKABLE QVariantMap snapSize();
    Q_INVOKABLE QVariantMap setSnapSize(const QVariant &size);
    Q_INVOKABLE bool snapToFloor();
    Q_INVOKABLE QVariantMap undoState();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool play();
    Q_INVOKABLE bool stop();
    Q_INVOKABLE bool pause();
    Q_INVOKABLE bool playing();
    Q_INVOKABLE bool simulate(bool enabled = true);
    Q_INVOKABLE bool frame(int n = 1, double dt = -1.0);
    Q_INVOKABLE QVariantMap warmUpShaders();
    Q_INVOKABLE QVariantMap viewportState();
    Q_INVOKABLE QVariantMap mirrorStats();
    Q_INVOKABLE QVariantMap screenshot(const QString &path, int width = 256, int height = 256,
                                       const QVariantList &probes = QVariantList(),
                                       const QVariant &grade = QVariant());
    Q_INVOKABLE bool beginBatch();
    Q_INVOKABLE bool endBatch();
    Q_INVOKABLE bool importAssets(const QVariant &paths);

private:
    /// id | [id] | null -> nodes, reporting an unknown id as a verb failure.
    /// Shared by editor.select and editor.selectAdd.
    bool resolveNodeArgument(const QVariant &id, const QString &verb,
                             QList<iris::SceneNodePtr> &out);
    /// The inclusive run from one node to another in VISIBLE OUTLINER ORDER
    /// (the hierarchy panel answers when there is one), or document pre-order
    /// with no window. Folder rows and the World root are never in it.
    QList<iris::SceneNodePtr> rangeInVisibleOrder(const iris::SceneNodePtr &a,
                                                  const iris::SceneNodePtr &b);

    int mBatchDepth = 0;
};

#endif // SCRIPTING_EDITORAPI_H
