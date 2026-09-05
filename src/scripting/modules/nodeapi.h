/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCRIPTING_NODEAPI_H
#define SCRIPTING_NODEAPI_H

// node.* — operations on one scene node by id (SCRIPTING_SPEC §1.2).
//
// remove/duplicate go through the parameterised MainWindow verbs; reparent and
// transform push their commands directly (the two class-A commands); property
// get/set ride the SceneNode reflection (getPropertyValue + the new
// setPropertyValue, lights and transforms first).

#include <functional>

#include <QStringList>
#include <QVariantMap>

#include "scripting/apimodule.h"
#include "irisgl/irisglfwd.h"

class NodeApi : public ApiModule
{
    Q_OBJECT
public:
    using ApiModule::ApiModule;

    QString jsName() const override { return QStringLiteral("node"); }
    QVector<VerbInfo> verbs() const override;

    Q_INVOKABLE bool remove(const QString &id);
    Q_INVOKABLE QString duplicate(const QString &id);
    Q_INVOKABLE bool reparent(const QString &id, const QString &parentId);
    Q_INVOKABLE QVariantMap transform(const QString &id, const QVariantMap &change = QVariantMap());
    Q_INVOKABLE QVariant property(const QString &id, const QString &key);
    Q_INVOKABLE bool setProperty(const QString &id, const QString &key, const QVariant &value);
    Q_INVOKABLE QVariant properties(const QString &id);
    Q_INVOKABLE QVariant info(const QString &id);
    Q_INVOKABLE QVariant boneNames(const QString &id);
    Q_INVOKABLE QString skinningMode(const QString &id);
    Q_INVOKABLE bool setLightProfile(const QString &id, const QString &assetGuid);
    Q_INVOKABLE QVariant lightProfile(const QString &id);
    Q_INVOKABLE bool setLightTexture(const QString &id, const QString &assetGuid);
    Q_INVOKABLE QVariant lightTexture(const QString &id);
    Q_INVOKABLE bool setDecalTexture(const QString &id, const QString &assetGuid);
    Q_INVOKABLE QVariant decalTexture(const QString &id);
    Q_INVOKABLE bool setParticleTexture(const QString &id, const QString &assetGuid);
    Q_INVOKABLE QVariant particleTexture(const QString &id);
    Q_INVOKABLE bool setPlanarReflector(const QString &id, bool enabled);
    Q_INVOKABLE bool planarReflector(const QString &id);
    Q_INVOKABLE bool setStatic(const QString &id, bool value);
    Q_INVOKABLE bool isStatic(const QString &id);
    Q_INVOKABLE bool physics(const QString &id, const QVariantMap &change = QVariantMap());
    Q_INVOKABLE QVariantMap physicsInfo(const QString &id);

    // ---- sockets (CAMERAS_SPEC §5/§6, D9) --------------------------------
    Q_INVOKABLE QVariantMap addSocket(const QString &id, const QVariantMap &socket);
    Q_INVOKABLE bool removeSocket(const QString &id, const QString &socketName);
    Q_INVOKABLE QVariantList sockets(const QString &id);
    Q_INVOKABLE bool attachToSocket(const QString &id, const QString &ownerId,
                                    const QString &socketName);
    Q_INVOKABLE bool detachFromSocket(const QString &id);

private:
    iris::SceneNodePtr nodeOrFail(const QString &id, const QString &verb);
    /// The node's reflected property names, in declaration order. The list the
    /// two "unknown property" errors quote, so a rejected key is followed by
    /// the keys that would have worked.
    static QStringList propertyKeys(const iris::SceneNodePtr &node);
    iris::LightNodePtr lightOrFail(const QString &id, const QString &verb);
    iris::DecalNodePtr decalOrFail(const QString &id, const QString &verb);
    /// The MeshNode with this guid, or null with a JS error already thrown —
    /// the socket verbs' "this thing cannot carry a socket" refusal.
    iris::MeshNodePtr meshOrFail(const QString &id, const QString &verb);

    /// Records an ALREADY-APPLIED node edit on the undo stack (F5). No-op when
    /// the session has no stack (--headless document runs, unit hosts).
    /// See commands/nodeeditcommand.h for the idempotency contract.
    void recordNodeEdit(const QString &text, std::function<void()> redoFn,
                        std::function<void()> undoFn);
};

#endif // SCRIPTING_NODEAPI_H
