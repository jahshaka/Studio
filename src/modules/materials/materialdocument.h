/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#pragma once

#include <QObject>
#include <QString>

#include "modules/materials/widgets/listwidget.h"   // shaderInfo + its Origin

class GraphNodeScene;
class NodeGraph;
class QTimer;
class QUndoStack;

namespace materials
{

/// ONE OPEN MATERIAL (MATERIALS_TABS_SPEC §2.1).
///
/// The Materials page used to hold exactly one of everything — one NodeGraph,
/// one GraphNodeScene, one QUndoStack, one autosave timer, one `shaderInfo` —
/// so opening a second material meant throwing the first one away. This is
/// that state, once per open material, and the page holds a vector of them in
/// tab order.
///
/// IDENTITY IS (guid, origin). The same guid at both origins is two documents:
/// a library original and the project's pinned copy are two things by the
/// four-drawer rule (widgets/listwidget.h shaderInfo::Origin), and neither
/// one's save may touch the other. `guid` empty is the ANONYMOUS document —
/// the untitled canvas the page boots on, at most one, which acquires a guid
/// in place the first time it is saved.
///
/// THE TIMER IS PER DOCUMENT, and that is not a detail: the page's single
/// 1.5 s autosave was never stopped on a material switch, so an edit made less
/// than 1.5 s before opening another material fired against the NEW material's
/// guid — the edit lost, the wrong material re-saved (the spec's C5).
class MaterialDocument : public QObject
{
    Q_OBJECT
public:
    explicit MaterialDocument(QObject *parent = nullptr);
    ~MaterialDocument() override;

    /// guid + name + origin: what this document IS.
    shaderInfo info;
    /// A shipped preset, open to be read (PRESET-UNIFY-1): the definition
    /// writer refuses its reserved guid, so this document's save stands down
    /// and its canvas takes no edits. `presetName` is the shipped name the
    /// banner quotes (a tile's label is elided; the preset's name is not).
    bool readOnly = false;
    QString presetName;
    /// A save of THIS document was refused and its scene issue is up.
    bool saveRefused = false;
    /// Latest-wins stamp for this document's asynchronous preview bakes. A
    /// bake started for one tab must not paint another tab's material.
    quint64 previewGeneration = 0;

    NodeGraph      *graph = nullptr;   ///< owned (freed with the scene, below)
    GraphNodeScene *scene = nullptr;   ///< owned
    QUndoStack     *stack = nullptr;   ///< owned (a child of this)
    QTimer         *saveTimer = nullptr; ///< owned; 1.5 s single-shot autosave

    bool isAnonymous() const { return info.GUID.isEmpty(); }
    bool is(const QString &guid, shaderInfo::Origin origin) const
    {
        return info.GUID == guid && info.origin == origin;
    }

    /// The tab's text. A PROJECT-origin document says so, because the same
    /// guid can be open at both scopes and the user must be able to tell which
    /// copy an edit will change.
    QString label() const;

    /// Take a graph and the scene built for it; the previous pair is freed.
    void adopt(NodeGraph *graph, GraphNodeScene *scene);

private:
    /// THE GRAPH DIES AFTER THE SCENE THAT DRAWS IT. Every GraphNode item
    /// holds a raw NodeModel* and the node's embedded QWidget belongs to a
    /// QGraphicsProxyWidget, so the scene must be gone before the graph is
    /// deleted — the graph's destructor is chained onto the scene's
    /// `destroyed` signal rather than guessed at.
    static void free(NodeGraph *dyingGraph, GraphNodeScene *dyingScene);
};

}
