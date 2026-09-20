/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/
#include "modules/materials/materialdocument.h"

#include <QTimer>
#include <QUndoStack>

#include "modules/materials/graph/graphnodescene.h"
#include "modules/materials/graph/nodegraph.h"

namespace materials
{

MaterialDocument::MaterialDocument(QObject *parent) : QObject(parent)
{
    stack = new QUndoStack(this);
    // THE GRAPH'S AUTOSAVE, per document. It began as a node-position debounce
    // and is now the one hook every edit reaches (GraphNodeScene::
    // graphInvalidated): a move, a connection, a deletion, a value —
    // "write THIS graph 1.5 s after its last change". The page connects the
    // timeout, because saving is the page's business.
    saveTimer = new QTimer(this);
    saveTimer->setSingleShot(true);
    saveTimer->setInterval(1500);
}

MaterialDocument::~MaterialDocument()
{
    free(graph, scene);
    graph = nullptr;
    scene = nullptr;
}

QString MaterialDocument::label() const
{
    const QString name = info.name.isEmpty() ? QObject::tr("Untitled") : info.name;
    return info.origin == shaderInfo::Origin::Project
               ? QObject::tr("%1 (project)").arg(name)
               : name;
}

void MaterialDocument::adopt(NodeGraph *newGraph, GraphNodeScene *newScene)
{
    // The pair that is being replaced — and only what is actually being
    // replaced: a "name this canvas" save rebuilds the scene around the SAME
    // graph, and freeing the graph there would free the one being adopted.
    NodeGraph *dyingGraph = (newGraph == graph) ? nullptr : graph;
    GraphNodeScene *dyingScene = (newScene == scene) ? nullptr : scene;
    graph = newGraph;
    scene = newScene;
    free(dyingGraph, dyingScene);
}

void MaterialDocument::free(NodeGraph *dyingGraph, GraphNodeScene *dyingScene)
{
    if (!dyingScene) {
        delete dyingGraph;
        return;
    }
    // ORDER, NOT TIMING: the scene is deleteLater'd (it can be replaced from
    // inside one of its own handlers — a tile dropped on the canvas opens a
    // material), so the graph goes when the scene actually goes, whenever that
    // is. Deleting the graph here would leave every GraphNode item holding a
    // freed NodeModel until the queued deletion ran.
    if (dyingGraph) {
        QObject::connect(dyingScene, &QObject::destroyed,
                         [dyingGraph]() { delete dyingGraph; });
    }
    dyingScene->deleteLater();
}

}
