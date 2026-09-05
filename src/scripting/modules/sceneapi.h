/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_SCENEAPI_H
#define SCRIPTING_SCENEAPI_H

// scene.* — reading the document and adding nodes (SCRIPTING_SPEC §1.2).
//
// Add verbs DELEGATE to the MainWindow verbs (§2.3: delegation is undo-correct
// and headless-proven); the new-node id is recovered from the selection the
// AddSceneNodeCommand makes. Options ({position, rotation, scale, parent})
// apply through Transform/Reparent commands so they join the same undo macro.

#include <QVariantList>
#include <QVariantMap>

#include "scripting/apimodule.h"
#include "irisgl/irisglfwd.h"

class SceneApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("scene"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE QVariantList nodes(const QVariant &options = QVariant());
    Q_INVOKABLE QVariant find(const QString &name);
    Q_INVOKABLE QString root();
    Q_INVOKABLE QVariant addPrimitive(const QString &name, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QString addLight(const QString &type, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QString addEmpty(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QString addViewer(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QString addMesh(const QString &path, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QString addImagePlane(const QString &textureGuid, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QString addDecal(const QString &textureGuid, const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QString addParticles(const QString &preset = QString(), const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QString addCamera(const QVariantMap &options = QVariantMap());
    Q_INVOKABLE QVariantList cameras();
    Q_INVOKABLE bool setActiveCamera(const QVariant &id = QVariant());
    Q_INVOKABLE QVariant activeCamera();

private:
    iris::ScenePtr sceneOrFail();
    QString finishAdd(const QVariantMap &options, const QString &verb);
    bool applyOptions(const iris::SceneNodePtr &node, const QVariantMap &options, const QString &verb);
};

#endif // SCRIPTING_SCENEAPI_H
