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

#include <QVariantMap>

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
    Q_INVOKABLE QString gizmoMode();
    Q_INVOKABLE bool setGizmoMode(const QString &mode);
    Q_INVOKABLE bool focusSelection();
    Q_INVOKABLE bool gameView(bool enabled);
    Q_INVOKABLE bool isGameView();
    Q_INVOKABLE double snapSize();
    Q_INVOKABLE bool setSnapSize(double size);
    Q_INVOKABLE bool snapToFloor();
    Q_INVOKABLE bool undo();
    Q_INVOKABLE bool redo();
    Q_INVOKABLE bool play();
    Q_INVOKABLE bool stop();
    Q_INVOKABLE bool simulate(bool enabled = true);
    Q_INVOKABLE bool frame(int n = 1);
    Q_INVOKABLE QVariantMap screenshot(const QString &path, int width = 256, int height = 256);
    Q_INVOKABLE bool beginBatch();
    Q_INVOKABLE bool endBatch();

private:
    int mBatchDepth = 0;
};

#endif // SCRIPTING_EDITORAPI_H
