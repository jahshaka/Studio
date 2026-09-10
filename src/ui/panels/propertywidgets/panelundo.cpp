/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/panelundo.h"

#include "commands/nodeeditcommand.h"
#include "commands/scenepropertycommand.h"
#include "commands/setnodepropertycommand.h"
#include "commands/worldmodecommand.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/services.h"
#include "services/undoservice.h"

namespace panelundo {

SceneRows::SceneRows(SceneFn scene, ServicesFn services, std::function<void()> refresh,
                     std::function<bool()> guard)
    : mScene(std::move(scene)), mServices(std::move(services)), mRefresh(std::move(refresh)),
      mGuard(std::move(guard))
{
}

rowundo::Binding SceneRows::operator()(const QString &key, const QString &text,
                                       std::function<QVariant(const QVariant &)> toDocument) const
{
    SceneFn sceneFn = mScene;
    ServicesFn servicesFn = mServices;
    std::function<void()> refresh = mRefresh;

    rowundo::Binding b;
    b.guard = mGuard;
    b.read = [sceneFn, key]() { return sceneprops::get(sceneFn(), key); };
    b.write = [sceneFn, key, toDocument](const QVariant &value) {
        auto scene = sceneFn();
        if (!scene) return;
        sceneprops::set(scene, key, toDocument ? toDocument(value) : value);
    };
    b.commit = [sceneFn, servicesFn, refresh, key, text](const QVariant &before,
                                                         const QVariant &after) {
        pushSceneEdit(servicesFn(), sceneFn(), key, text, before, after, refresh);
    };
    return b;
}

NodeRows::NodeRows(NodeFn node, ServicesFn services, std::function<bool()> guard)
    : mNode(std::move(node)), mServices(std::move(services)), mGuard(std::move(guard))
{
}

rowundo::Binding NodeRows::operator()(const QString &key,
                                      std::function<QVariant(const QVariant &)> toDocument) const
{
    NodeFn nodeFn = mNode;
    ServicesFn servicesFn = mServices;

    rowundo::Binding b;
    b.guard = mGuard;
    b.read = [nodeFn, key]() -> QVariant {
        auto node = nodeFn();
        return node ? node->getPropertyValue(key) : QVariant();
    };
    b.write = [nodeFn, key, toDocument](const QVariant &value) {
        auto node = nodeFn();
        if (!node) return;
        node->setPropertyValue(key, toDocument ? toDocument(value) : value);
    };
    b.commit = [nodeFn, servicesFn, key](const QVariant &before, const QVariant &after) {
        auto node = nodeFn();
        StudioServices *services = servicesFn();
        if (!node || !services || !services->undo) return;
        services->undo->push(new SetNodePropertyCommand(node, key, before, after));
    };
    return b;
}

void runWorldModeEdit(StudioServices *services, const iris::ScenePtr &scene, const QString &text,
                      const std::function<void()> &edit, std::function<void()> refresh)
{
    if (!scene || !edit) return;
    const auto before = WorldModeCommand::capture(scene);
    edit();
    if (refresh) refresh();
    if (!services || !services->undo) return;
    auto *cmd = new WorldModeCommand(text, scene, before);
    if (refresh) cmd->setRefresh(refresh);
    services->undo->push(cmd);
}

void pushSceneEdit(StudioServices *services, const iris::ScenePtr &scene, const QString &key,
                   const QString &text, const QVariant &before, const QVariant &after,
                   std::function<void()> refresh)
{
    if (!scene || !services || !services->undo) return;
    if (before == after) return;
    auto *cmd = new ScenePropertyCommand(text, scene, key, before, after);
    if (refresh) cmd->setRefresh(refresh);
    services->undo->push(cmd);
}

void pushEdit(StudioServices *services, const QString &text, std::function<void()> redoFn,
              std::function<void()> undoFn)
{
    if (!services || !services->undo) return;
    services->undo->push(new NodeEditCommand(text, std::move(redoFn), std::move(undoFn)));
}

}   // namespace panelundo
