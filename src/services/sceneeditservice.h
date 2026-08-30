/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEEDITSERVICE_H
#define SCENEEDITSERVICE_H

// SceneEditService — the scene-mutation verbs (APP_ARCHITECTURE_AUDIT §3.3).
//
// The document/DB halves of MainWindow's 26 add-verbs, the addNodeToScene
// funnel, delete/duplicate, material presets, material creation and node
// export. Every mutation still goes through the same undo commands, so the
// hierarchy/selection refreshes the commands perform are unchanged; the few
// direct widget refreshes MainWindow used to do inline are emitted as signals
// the shell connects to its panels.
//
// Both surfaces call in here: MainWindow's slots are one-line delegators and
// the scripting Scene/Node/Material/Assets modules call the same methods —
// one implementation per verb (SCRIPTING_SPEC §2.3's seam, extracted).

#include <QObject>
#include <QString>
#include <QVector3D>

#include "irisgl/irisglfwd.h"
#include "../core/project.h"   // ModelTypes

#include <functional>

class Database;
class IEditorViewport;
class UndoService;
class SelectionService;
class MaterialPreset;

class SceneEditService : public QObject
{
    Q_OBJECT

public:
    SceneEditService(Database *db,
                     Project *project,
                     UndoService *undo,
                     SelectionService *selection,
                     IEditorViewport *viewport,
                     std::function<iris::ScenePtr()> sceneProvider,
                     QObject *parent = nullptr);

    /// The open scene (the document) — also the scene accessor the scripting
    /// modules use instead of MainWindow::getScene().
    iris::ScenePtr scene() const { return sceneProvider ? sceneProvider() : iris::ScenePtr(); }

    // Built-in primitives (each pairs a bundled mesh with a DB object row).
    void addPlane();
    void addGround();
    void addCone();
    void addCapsule();
    void addCube();
    void addTorus();
    void addSphere();
    void addCylinder();
    void addPyramid();
    void addTeapot();
    void addSponge();
    void addSteps();
    void addGear();
    /// Name-dispatch over the primitives above ("Plane", "Cone", ...).
    void addPrimitive(const QString &name);

    void addPointLight();
    void addSpotLight();
    void addDirectionalLight();
    void addAreaLight();

    void addEmpty();
    void addViewer();
    void addParticleSystem();

    /// Imports a mesh file straight into the scene. The path must be a real
    /// file — the file dialog stays in the shell.
    void addMesh(const QString &path, bool ignore = false, QVector3D position = QVector3D());
    /// Instantiates a stored object asset (drag-drop / assets.addToScene).
    void addMaterialMesh(const QString &path, bool ignore, QVector3D position,
                         const QString &guid, const QString &assetName);
    void addAssetParticleSystem(bool ignore, QVector3D position, QString guid,
                                QString assetName);

    /// Parents to the selection (or the root) and refreshes the hierarchy.
    void addNodeToActiveNode(iris::SceneNodePtr sceneNode);
    /// The funnel every add-verb ends in: default material, spawn offset,
    /// undoable AddSceneNodeCommand.
    void addNodeToScene(iris::SceneNodePtr sceneNode, bool ignore = false);

    bool deleteNode(iris::SceneNodePtr node);
    iris::SceneNodePtr duplicateNode(iris::SceneNodePtr node);

    /// Applies a material preset to the selected mesh node (both surfaces
    /// select first — the pre-extraction contract).
    void applyMaterialPreset(const MaterialPreset &preset);

    /// Snapshots the node's material into a .material asset registered under
    /// folderGuid (the shell passes its asset browser's current folder).
    void createMaterialFromNode(iris::SceneNodePtr node, const QString &folderGuid);

    /// Packages a node with its dependencies into a .jaf at filePath (the
    /// save dialog stays in the shell).
    void exportNodeTo(const iris::SceneNodePtr &node, ModelTypes modelType,
                      const QString &filePath);

signals:
    /// The scene tree changed outside an undo command (addNodeToActiveNode).
    void hierarchyChanged();
    /// A material asset was created — the asset browser should re-list.
    void assetViewRefreshRequested();
    /// A preset landed on the selected node — the properties panel should
    /// refresh its material section for this preset type.
    void materialApplied(const QString &presetType);

private:
    void addBuiltinPrimitive(const QString &meshPath, const QString &name);

    Database *db;
    Project *project;
    UndoService *undo;
    SelectionService *selection;
    IEditorViewport *viewport;
    std::function<iris::ScenePtr()> sceneProvider;
};

#endif // SCENEEDITSERVICE_H
