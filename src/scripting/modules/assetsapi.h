/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_ASSETSAPI_H
#define SCRIPTING_ASSETSAPI_H

// assets.* — the store, the project, the scene (SCRIPTING_SPEC §1.3).
//
// import runs the extracted AssetImporter service (headless, mesh files);
// addToProject is the widget's post-Toast body as a service (file copies +
// Database::copyAsset + AssetManager registrations); addToScene delegates to
// MainWindow::addMaterialMesh (undo-correct today). Asset mutations are NOT
// undoable (§1.6.5) — permanent, and documented as such.

#include <QVariantList>
#include <QVariantMap>

#include "../apimodule.h"

class AssetsApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("assets"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantList list(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QString import(const QString &path);
    Q_INVOKABLE QString addToProject(const QString &guid);
    Q_INVOKABLE QString addToScene(const QString &guid, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantList builtins();
    Q_INVOKABLE bool remove(const QString &guid, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE bool refreshThumbnail(const QString &guid);
    Q_INVOKABLE QVariantList dependencies(const QString &guid);
};

#endif // SCRIPTING_ASSETSAPI_H
