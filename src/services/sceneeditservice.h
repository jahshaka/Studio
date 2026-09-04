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

#include "irisgl/core/math/vec.h"
#include <QObject>
#include <QString>

#include "irisgl/irisglfwd.h"
#include "data/project.h"   // ModelTypes

#include <functional>

class Database;
class IEditorViewport;
class UndoService;
class SelectionService;
class MaterialPreset;

/// Options for SceneEditService::addImagePlane. Defaults are the
/// owner-approved §8 calls (IMAGE_PLANE_SPEC): double-sided ON (a
/// single-sided floating image that vanishes from behind reads as a bug).
struct ImagePlaneOptions
{
    bool doubleSided = true;
};

/// Options for SceneEditService::addDecal (DECALS_SPEC §5.5). Defaults are a
/// 1 m sticker half a metre deep — big enough to see, small enough not to
/// swallow the scene.
struct DecalOptions
{
    float width  = 1.0f;      ///< local X extent
    float height = 1.0f;      ///< local Z extent (the image's V axis)
    float depth  = 0.5f;      ///< local Y extent = projection thickness
    float metalness = 0.0f;
    float roughness = 1.0f;
    bool  ignoreAlphaDiffuse = false;
    /// Skip the spawn offset (a drop point decided the position already).
    bool  positionGiven = false;
    iris::Vec3 position;
};

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
    /// Adds an emitter, optionally stamped from a preset recipe
    /// (PARTICLES_FX2_SPEC §6). Returns the node so callers — the scripting
    /// verb, chiefly — can address what they just made; null when there is no
    /// scene. Undoable through addNodeToScene like every other add.
    /// No default argument: iris::ParticlePreset is only forward-declared here
    /// (naming an enumerator would pull particlesystemnode.h into every
    /// translation unit that touches this service). Callers pass
    /// iris::ParticlePreset::Custom for a plain emitter.
    iris::ParticleSystemNodePtr addParticleSystem(iris::ParticlePreset preset);

    /// IMAGE_PLANE_SPEC Option A: spawns an image plane for a Texture asset
    /// at `position` — a plane.obj mesh scaled to the image's aspect (max
    /// side 1 m), oriented ONCE at creation to face the editor camera
    /// (afterwards a perfectly normal node: gizmos, undo, serialization),
    /// with a basic PBR material carrying the image as baseColorMap
    /// (roughness 1, metallic 0; alpha images blend — ImageMaterial::
    /// fromTexture is the shared builder). Bytes resolve pin-first through
    /// the CAS. Lands as one undoable AddSceneNodeCommand plus an
    /// Object→Texture dependency row for the export walkers.
    /// Returns null when the guid is not a resolvable image.
    iris::MeshNodePtr addImagePlane(const QString &textureGuid, iris::Vec3 position,
                                    const ImagePlaneOptions &opts = ImagePlaneOptions());

    /// DECALS_SPEC: spawns a projected-texture decal bound to a Texture asset.
    /// `textureGuid` may be empty — the node is created with no image, drawing
    /// its wire box until one is picked in the panel or dropped on it.
    ///
    /// The image is a BINDING, not a direct add: the guid is pinned into the
    /// project as ProjectAssets::AddKind::Binding (no companion PBR material is
    /// minted — the user asked for a decal, not a material) and an
    /// Object->Texture dependency row is written for the export walkers.
    /// Bytes resolve pin-first through the CAS.
    /// Returns null when there is no scene. Undoable (one AddSceneNodeCommand).
    iris::DecalNodePtr addDecal(const QString &textureGuid,
                                const DecalOptions &opts = DecalOptions());
    /// Rebinds a decal's image (the panel's picker and the asset-bin drop both
    /// land here): pins the guid as a dependency, rewrites the dependency row
    /// and re-resolves the path. False when the node is not a decal.
    bool setDecalTexture(const iris::DecalNodePtr &decal, const QString &textureGuid);
    /// Binds (or, with an empty guid, clears) an emitter's particle image, the
    /// same way setDecalTexture binds a decal's: a BINDING membership and a
    /// dependency row, never a copy and never a companion material. Until this
    /// existed the only way to change an emitter's image was the property
    /// panel's picker — nothing scripted could do it, which is why the sample
    /// scene could not be re-authored from a script.
    bool setParticleTexture(const iris::ParticleSystemNodePtr &emitter,
                            const QString &textureGuid);

    /// Imports a mesh file straight into the scene. The path must be a real
    /// file — the file dialog stays in the shell.
    void addMesh(const QString &path, bool ignore = false, iris::Vec3 position = iris::Vec3());
    /// Instantiates a stored object asset (drag-drop / assets.addToScene).
    void addMaterialMesh(const QString &path, bool ignore, iris::Vec3 position,
                         const QString &guid, const QString &assetName);
    void addAssetParticleSystem(bool ignore, iris::Vec3 position, QString guid,
                                QString assetName);

    /// Parents to the selection (or the root) and refreshes the hierarchy.
    void addNodeToActiveNode(iris::SceneNodePtr sceneNode);
    /// The funnel every add-verb ends in: default material, spawn offset,
    /// undoable AddSceneNodeCommand.
    void addNodeToScene(iris::SceneNodePtr sceneNode, bool ignore = false);

    bool deleteNode(iris::SceneNodePtr node);
    iris::SceneNodePtr duplicateNode(iris::SceneNodePtr node);

    /// Applies a material preset to the selection. The selection may be a
    /// single mesh OR a container (an imported model roots at an Empty — the
    /// viewport's click-selects-the-root rule hands exactly that node over):
    /// every mesh at or under it receives its own fresh material instance.
    /// Undoable as one "Apply Material" entry. The old mesh-only guard
    /// silently dropped presets applied to models — the owner-reported
    /// "PBR materials lost on reopen" data loss: they never entered the
    /// document, so the writer had nothing to save.
    void applyMaterialPreset(const MaterialPreset &preset);
    void applyMaterialPreset(const MaterialPreset &preset, iris::SceneNodePtr target);

    /// Applies a SAVED material asset (a project .material row — e.g. one the
    /// preset apply registered under Presets/) to the same target set.
    /// Dispatches on the stored materialType, so saved PBR materials come back
    /// as real PbrMaterials. Returns false when the guid has no material data
    /// or the target holds no meshes.
    bool applyMaterialAsset(const QString &assetGuid, iris::SceneNodePtr target);

    /// Snapshots the node's material into a .material asset registered under
    /// folderGuid (the shell passes its asset browser's current folder).
    void createMaterialFromNode(iris::SceneNodePtr node, const QString &folderGuid);

    /// Packages a node with its dependencies into a .jaf at filePath (the
    /// save dialog stays in the shell).
    void exportNodeTo(const iris::SceneNodePtr &node, ModelTypes modelType,
                      const QString &filePath);

    // Refresh notifications the undo commands raise (Phase 4: the commands'
    // widget-refresh statics became these signals; the shell connects them to
    // its panels). Public emit-wrappers because QUndoCommands are not QObjects.
    void notifyNodeInserted(const iris::SceneNodePtr &node);
    void notifyNodeRemoved(const iris::SceneNodePtr &node);
    void notifyHierarchyChanged();
    void notifyTransformChanged();

signals:
    /// An undo command inserted/removed a node — the hierarchy panel should
    /// add/remove the matching row.
    void nodeInserted(const iris::SceneNodePtr &node);
    void nodeRemoved(const iris::SceneNodePtr &node);
    /// A node's transform changed outside the properties panel — it should
    /// re-read the selection's transform.
    void transformRefreshRequested();
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
