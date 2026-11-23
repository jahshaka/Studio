/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "ui/panels/propertywidgets/panelundo.h"

#include "app/qtstdgui.h"

#include "commands/nodeeditcommand.h"
#include "commands/scenepropertycommand.h"
#include "commands/setnodepropertycommand.h"
#include "commands/worldmodecommand.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "services/editgate.h"
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
    b.key = key;                     // the property filter's row key (rowundo.h)
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
    b.key = key;                     // the property filter's row key (rowundo.h)
    b.guard = mGuard;
    // THE NODE's EDGE (NOQT-DOCUMENT-1 stage 4, walk 1): the row reads the node's value as the QVariant it was, and
    // writes back through the conversion by the property's kind (qtstd::toNodeValue). The widget made a value of
    // that kind, so the write is not refused (its answer is not read); the step records the two Values.
    const std::string name = key.toStdString();
    b.read = [nodeFn, name]() -> QVariant {
        auto node = nodeFn();
        return node ? qtstd::nodeVariant(*node, name) : QVariant();
    };
    b.write = [nodeFn, name, toDocument](const QVariant &value) {
        auto node = nodeFn();
        if (!node) return;
        if (const auto v = qtstd::toNodeValue(*node, name, toDocument ? toDocument(value) : value))
            node->setPropertyValue(name, *v);
    };
    b.commit = [nodeFn, servicesFn, name](const QVariant &before, const QVariant &after) {
        auto node = nodeFn();
        StudioServices *services = servicesFn();
        if (!node || !services || !services->undo) return;
        const auto was = qtstd::toNodeValue(*node, name, before);
        const auto now = qtstd::toNodeValue(*node, name, after);
        // A value the edge cannot convert is NOT recorded as a Null (a Null is a value: a colour's none, a postFx
        // clear): the step is skipped (the reader's F8; read() gives nodeVariant's own values, so it does not occur).
        if (!was || !now) return;
        services->undo->push(new SetNodePropertyCommand(node, name, *was, *now));
    };
    return b;
}

void runWorldModeEdit(StudioServices *services, const iris::ScenePtr &scene, const QString &text,
                      const std::function<void()> &edit, std::function<void()> refresh)
{
    if (!scene || !edit) return;
    // THE EDIT GATE, BEFORE THE EDIT (owner, ledger §423; services/editgate.h).
    // This helper APPLIES and then records, so the gate has to be asked before
    // `edit()` runs — a refusal at the push would leave the registry written
    // with no step behind it.
    if (editgate::refuse()) return;
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
    if (!scene) return;
    if (before == after) return;
    // THE EDIT GATE, WITH THE UNDO ITS CALLER NEVER GOT TO WRITE (ledger §423).
    // Callers of this helper have ALREADY written the document — that is what
    // it is for — so refusing here means putting the value back, and `before`
    // is exactly the state the undo step would have restored. Asked before the
    // undo service is required: a host with no undo stack still may not be
    // edited by hand while a script runs.
    if (editgate::refuse()) {
        sceneprops::set(scene, key, before);
        if (refresh) refresh();
        return;
    }
    if (!services || !services->undo) return;
    auto *cmd = new ScenePropertyCommand(text, scene, key, before, after);
    if (refresh) cmd->setRefresh(refresh);
    services->undo->push(cmd);
}

void pushEdit(StudioServices *services, const QString &text, std::function<void()> redoFn,
              std::function<void()> undoFn)
{
    // As above: NodeEditCommand's contract is "applied and verified first,
    // pushed after", so a refusal here runs the undo half the caller handed in
    // rather than dropping a command that has already happened.
    if (editgate::refuse()) {
        if (undoFn) undoFn();
        return;
    }
    if (!services || !services->undo) return;
    services->undo->push(new NodeEditCommand(text, std::move(redoFn), std::move(undoFn)));
}

}   // namespace panelundo
