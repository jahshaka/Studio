/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef PANELUNDO_H
#define PANELUNDO_H

// panelundo — what a properties-panel row's undo step IS (debt L6 / N5).
//
// rowundo.h owns the GESTURE (when a step is committed); this owns the MODEL
// (which command carries it), so that every panel reaches the same three
// answers instead of inventing a fourth:
//
//   * a WORLD property (fog, ambient, gravity, the GI volume, a post-process
//     parameter, the looks stack, the sky block) -> ScenePropertyCommand,
//     through the sceneprops table;
//   * a NODE property (a light's intensity, a decal's width, an emitter's
//     rate, a camera's exposure) -> SetNodePropertyCommand, through the node's
//     own reflection — the same call node.setProperty makes, so the panel and
//     the verb are one code path;
//   * a QUALITY-REGISTRY row (msaa, shadow resolution, sky detail, ambient
//     from sky, the Rayon rows, the post-process on/off rows) ->
//     WorldModeCommand, because those rows write a backing field AND pin
//     themselves against the tier, and an undo that restored the value without
//     the pin would leave the scene lying about what the tier owns.
//
// Everything here no-ops without an undo stack: headless hosts and the panel
// suites drive the same rows with `services == nullptr` and the edits still
// apply, exactly as they did before this existed.

#include <functional>

#include <QString>
#include <QVariant>

#include "irisgl/irisglfwd.h"
#include "ui/panels/propertywidgets/rowundo.h"

struct StudioServices;

namespace panelundo {

using SceneFn = std::function<iris::ScenePtr()>;
using NodeFn = std::function<iris::SceneNodePtr()>;
using ServicesFn = std::function<StudioServices *()>;

/// Rows that write the open scene's world properties.
///
/// The scene and the services are read through the getters at SIGNAL time, not
/// captured: a panel is built once and re-bound to a different scene on every
/// project open, and rows wired to the scene of the day would write into a
/// document nobody is looking at.
class SceneRows
{
public:
    /// `guard` is the panel's "not populating" predicate (rowundo::Binding).
    SceneRows(SceneFn scene, ServicesFn services, std::function<void()> refresh = {},
              std::function<bool()> guard = {});

    /// A row bound to the world property `key` (see sceneprops::ids()).
    /// `toDocument` maps the control's own value onto the document's — a combo
    /// row index onto an enum, a percentage onto a fraction. Omitted = the
    /// control's value IS the document's.
    rowundo::Binding operator()(const QString &key, const QString &text,
                                std::function<QVariant(const QVariant &)> toDocument = {}) const;

private:
    SceneFn mScene;
    ServicesFn mServices;
    std::function<void()> mRefresh;
    std::function<bool()> mGuard;
};

/// Rows that write the selected node's reflected properties.
class NodeRows
{
public:
    NodeRows(NodeFn node, ServicesFn services, std::function<bool()> guard = {});

    /// A row bound to the reflected property `key` (the node.setProperty key).
    rowundo::Binding operator()(const QString &key,
                                std::function<QVariant(const QVariant &)> toDocument = {}) const;

private:
    NodeFn mNode;
    ServicesFn mServices;
    std::function<bool()> mGuard;
};

/// One quality-registry row edit: snapshot, apply, push (the shape
/// WorldModesPropertyWidget introduced, shared rather than copied). `refresh`
/// runs after the edit AND after every undo/redo of it.
void runWorldModeEdit(StudioServices *services, const iris::ScenePtr &scene, const QString &text,
                      const std::function<void()> &edit, std::function<void()> refresh = {});

/// One world-property edit applied by the caller, recorded here. For rows whose
/// write is more than a field assignment (the sky block, a texture rebind) and
/// which therefore cannot go through SceneRows' generic write.
void pushSceneEdit(StudioServices *services, const iris::ScenePtr &scene, const QString &key,
                   const QString &text, const QVariant &before, const QVariant &after,
                   std::function<void()> refresh = {});

/// An edit whose apply half is a SERVICE CALL rather than a value write
/// (NodeEditCommand's contract: applied and verified first, pushed after,
/// `redoFn` idempotent).
void pushEdit(StudioServices *services, const QString &text, std::function<void()> redoFn,
              std::function<void()> undoFn);

}   // namespace panelundo

#endif   // PANELUNDO_H
