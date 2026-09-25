/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "irisgl/core/math/mat3.h"
#include "irisgl/core/math/quat.h"
#include "irisgl/core/math/vec.h"
#include "services/sceneeditservice.h"

#include "irisgl/document/assets/mesh.h"

#include <functional>

#include <algorithm>

#include "services/assetcas.h"
#include "services/assetclosure.h"
#include "services/assetstorepaths.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPointer>
#include <QPixmap>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QTextStream>

#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/physics/environment.h"

namespace { void regenerateGuids(const iris::SceneNodePtr &root,
                                 QHash<QString, QString> *guidMapOut = nullptr); }
#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeset.h"
#include "irisgl/document/animation/keyframeanimation.h"

#include "commands/addscenenodecommand.h"
#include "commands/changematerialcommand.h"
#include "commands/pinassetcommand.h"
#include "commands/resetmaterialcommand.h"
#include "commands/deletescenenodecommand.h"
#include "commands/nodeeditcommand.h"
#include "data/constants.h"
#include "data/primitives.h"
#include "services/assethelper.h"
#include "services/meshbakestore.h"
#include "services/editgate.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/materialpreset.h"
#include "irisgl/core/logger.h"
#include "services/assetmetadata.h"
#include "services/nodenaming.h"
#include "services/imagematerial.h"
#include "services/materialdefaults.h"
#include "services/materialbundle.h"
#include "services/materialpresetassets.h"
#include "services/presetedit.h"
#include "services/materialpresetseeder.h"
#include "services/projectassets.h"
#include "services/shippedassets.h"
#include "services/scenenodehelper.h"
#include "services/thumbnailmanager.h"
#include "viewport/ieditorviewport.h"
#include "services/thumbnailgenerator.h"
#include "bridge/enginehost.h"
#include "io/assetmanager.h"
#include "io/ziphelper.h"
#include "io/materialreader.h"
#include "io/materialpresets.h"
#include "io/builtinmaterials.h"
#include "services/materialpreviewservice.h"
#include "io/scenereader.h"
#include "io/scenewriter.h"
#include "services/selectionservice.h"
#include "services/undoservice.h"

#include "zip.h"

SceneEditService::SceneEditService(Database *db,
                                   Project *project,
                                   UndoService *undo,
                                   SelectionService *selection,
                                   IEditorViewport *viewport,
                                   std::function<iris::ScenePtr()> sceneProvider,
                                   QObject *parent)
    : QObject(parent),
      db(db), project(project), undo(undo), selection(selection),
      viewport(viewport), sceneProvider(std::move(sceneProvider))
{
}

void SceneEditService::notifyNodeInserted(const iris::SceneNodePtr &node) { emit nodeInserted(node); }
void SceneEditService::notifyNodeRemoved(const iris::SceneNodePtr &node) { emit nodeRemoved(node); }
void SceneEditService::notifyHierarchyChanged() { emit hierarchyChanged(); }
void SceneEditService::notifyTransformChanged() { emit transformRefreshRequested(); }

void SceneEditService::addPrimitive(const QString &text,
                                    const std::optional<iris::Vec3> &position,
                                    surfaceplacement::Placement placement)
{
    // ONE TABLE (src/data/primitives.h). The node takes the row's NAME as its
    // own — there is no separate "node name" column any more: the two were
    // equal for every row but Capsule, where the second copy said "Plane" and
    // had done since the original addCapsule() (the render audit's A10).
    //
    // AND THE MESH IS THE SEEDED, BAKED ASSET (ATOM P2): the same geometry an
    // imported model gets, with its LOD chain, its cards and its SDF, resolved
    // through the seed key the document stores. `addBuiltinPrimitive` is gone —
    // it was this function's body with a mesh PATH for an argument, and the path
    // now comes out of the same table the name does.
    const primitives::Def *def = primitives::byName(text);
    if (!def) return;
    const QString name = QString::fromLatin1(def->name);

    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        QString::fromLatin1(def->mesh), name, nodeGuid, db);
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(
        nodeGuid, node->getName(),
        static_cast<int>(ModelTypes::Object),
        project->getProjectGuid(),
        project->getProjectGuid(),
        QString(),
        QString(),
        QByteArray(),
        QJsonDocument(props).toJson(),
        QByteArray(),
        QByteArray()
    );
    // WHERE THE MOUSE IS, when the caller knows (smoke S2): a drop carries the
    // ray-cast surface/ground point under the cursor, and the funnel's own
    // "in front of the camera" placement must not overwrite it. `ignore` is
    // that switch, spelled the way every other dropped add spells it.
    //
    // …AND ON TOP OF IT, not inside it (owner, 2026-09-14): every built-in
    // primitive is modelled around its own centre, so a drop that put the
    // PIVOT on the floor buried half of it. Placement::OnSurface rests the
    // node's bounding box on the point; a script asking for coordinates still
    // gets its pivot exactly there (services/surfaceplacement.h).
    if (position) surfaceplacement::place(node, *position, placement);
    addNodeToScene(node, position.has_value());
}

// The menu slots, over the ONE table below (they used to carry a second copy
// of every resource path — thirteen strings maintained in two places).
void SceneEditService::addGround()   { addPrimitive(QStringLiteral("Ground")); }
void SceneEditService::addCone()     { addPrimitive(QStringLiteral("Cone")); }
void SceneEditService::addCube()     { addPrimitive(QStringLiteral("Cube")); }
void SceneEditService::addTorus()    { addPrimitive(QStringLiteral("Torus")); }
void SceneEditService::addSphere()   { addPrimitive(QStringLiteral("Sphere")); }
void SceneEditService::addCylinder() { addPrimitive(QStringLiteral("Cylinder")); }
void SceneEditService::addTube()     { addPrimitive(QStringLiteral("Tube")); }
// (addTeapot / addSponge / addSteps / addGear are DELETED — owner review R6:
// PRIMITIVES ONLY. The four slots were dead in MainWindow too; nothing but
// this line ever called them.)

void SceneEditService::addPointLight()
{
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Point);
    node->icon = iris::Texture2D::load(":/icons/bulb.png");
    node->setName("Point Light");
    node->intensity = 1.0f;
    node->distance = 40.0f;
    addNodeToScene(node);
}

void SceneEditService::addSpotLight()
{
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Spot);
    node->icon = iris::Texture2D::load(":/icons/spotlight.png");
    node->setName("Spot Light");
    addNodeToScene(node);
}

void SceneEditService::addDirectionalLight()
{
    auto node = iris::LightNode::create();
    // (The re-statement of ShadowMapType::Soft that used to be here is gone —
    // CRUD: the constructor is the ONE place the shadow default lives, and a
    // second copy of it is a second thing to forget.)
    node->setLightType(iris::LightType::Directional);
    node->icon = iris::Texture2D::load(":/icons/light.png");   // the sun glyph
    node->setName("Directional Light");
    // AUTOMATIC FORWARD SHADING PRIORITY (owner decision Q1): the first
    // directional light in a scene takes 0 and IS the sun; a second slots into
    // 1, a third into 2. Nothing is decided by creation luck, and the author
    // can still change the row afterwards.
    if (auto s = scene()) node->forwardShadingPriority = s->nextForwardShadingPriority();
    addNodeToScene(node);
}

void SceneEditService::addAreaLight()
{
    // Engine viewport only (the menu entry is hidden in legacy mode): Ogre-Next's
    // rectangular area lights. No bundled icon glyph — SceneMirror draws one.
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Area);
    node->setName("Area Light");
    node->intensity = 1.0f;
    node->distance = 10.0f;
    node->rectWidth = 1.0f;
    node->rectHeight = 1.0f;
    addNodeToScene(node);
}

void SceneEditService::addSkyLight()
{
    // A light with no place and no direction: the scene's ambient, read from
    // the World sky. `color` is a TINT and `intensity` the strength; the panel
    // hides every other row and the mirror never gives it an Ogre::Light.
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Sky);
    node->icon = iris::Texture2D::load(":/icons/light.png");
    node->setName("Sky Light");
    node->intensity = 1.0f;
    node->color = QColor(255, 255, 255);
    addNodeToScene(node);
}

void SceneEditService::addEmpty()
{
    auto node = iris::SceneNode::create();
    node->setName("Empty");
    addNodeToScene(node);
}

iris::CameraNodePtr SceneEditService::addCamera(bool ignorePlacement)
{
    if (!scene()) return iris::CameraNodePtr();

    auto node = iris::CameraNode::create();
    node->setName("Camera");
    // A scene camera is a document object the user places, points and keys, so
    // it takes the normal spawn-in-front-of-the-editor-camera placement.
    //
    // THE FIRST ONE BECOMES THE ACTIVE CAMERA (PLAYER-SPAWN-1 rule 2, owner
    // 2026-09-17 — this comment used to say an add is never a side effect at
    // all). A scene with no camera plays through the free viewer, which is
    // wherever the editor was standing; putting the first camera in the scene
    // IS the statement "play through this". A SECOND camera still never steals
    // the shot — past the first, the choice has two answers and stays explicit
    // and saved (scene.setActiveCamera). The rule lives on the document
    // (Scene::armCameraIfNoneActive) and is applied by the add COMMAND, so an
    // undo/redo of this add arms and disarms symmetrically.
    addNodeToScene(node, ignorePlacement);
    return node;
}

iris::ParticleSystemNodePtr SceneEditService::addParticleSystem(iris::ParticlePreset preset)
{
    if (!scene()) return iris::ParticleSystemNodePtr();

    auto node = iris::ParticleSystemNode::create();
    node->setName(preset == iris::ParticlePreset::Custom
                      ? QStringLiteral("Particle System")
                      : iris::ParticleSystemNode::presetName(preset).left(1).toUpper() +
                            iris::ParticleSystemNode::presetName(preset).mid(1));
    // The recipe first: it resets every authoring field, so anything set here
    // afterwards (the texture, below) survives and anything set before does not.
    if (preset != iris::ParticlePreset::Custom) node->applyPreset(preset);

    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    // The "Systems" folder and the emitter's ParticleSystem row belong to a
    // PROJECT: with none open (a headless script, the startup placeholder)
    // they used to land in the library stamped with an empty project guid, one
    // row per emitter nobody could see (15c review #1 — the same guard the
    // material presets and the default Ground row got in 15c).
    const bool haveProject = project && !project->getProjectGuid().isEmpty();
    if (haveProject) {
        // THE EXISTING FOLDER'S GUID (small-items round B). This minted a fresh
        // guid, created "Systems" only when it was not there, and filed the
        // emitter's row under the fresh guid either way — so the first emitter
        // landed in the folder and every emitter after it was parented to a
        // guid nothing owned: a row that exists, resolves by guid, and appears
        // in no folder the asset panel can open.
        const QString fguid = db->ensureFolder(QStringLiteral("Systems"),
                                               project->getProjectGuid(), false);
        if (fguid.isEmpty()) return iris::ParticleSystemNodePtr();
        QJsonObject props;
        db->createAssetEntry(
            nodeGuid, node->getName(),
            static_cast<int>(ModelTypes::ParticleSystem),
            fguid,
            project->getProjectGuid(),
            QString(),
            QString(),
            QByteArray(),
            QJsonDocument(props).toJson(),
            QByteArray(),
            QByteArray()
        );
    }

    // The default particle image — a LIBRARY TEXTURE pinned into the project
    // (plan item 15c), bound through setParticleTexture, the same door the
    // emitter panel's image row and node.setParticleTexture use: pin, the
    // ParticleSystem->Texture dependency, the pinned bytes. ONE row per
    // library, not per emitter or per project — the shipped file is identified
    // by its content, so the fifth emitter and the next project reuse the row
    // the first one imported. (It used to be a QFile::copy into the project
    // folder plus a bare "Glowing Particle.jpg" row found again BY NAME, and
    // before that a fresh row per add.) With no project open there is nothing
    // to pin into: the emitter renders the shipped file and nothing is saved.
    const ShippedAssets::Pinned image = ShippedAssets::pinTexture(
        IrisUtils::getAbsoluteAssetPath("app/images/default_particle.jpg"),
        QStringLiteral("Glowing Particle.jpg"), db, project,
        // The user added the emitter and this is the picture it draws: content
        // the project HAS, and a tray tile like any other image in the scene.
        ShippedAssets::Ownership::Project);
    if (!image.guid.isEmpty()) setParticleTexture(node, image.guid);
    else if (!image.path.isEmpty()) node->setTexture(iris::Texture2D::load(image.path));
    if (!image.error.isEmpty())
        irisLog("addParticleSystem: the default particle image was not bound - " + image.error);

    addNodeToScene(node);
    return node;
}

int SceneEditService::refreshAssetMeshes(const QString &meshGuid, const QString &sourcePath)
{
    if (meshGuid.isEmpty() || sourcePath.isEmpty()) return 0;
    auto live = scene();
    if (!live || !live->getRootNode()) return 0;

    // The bake the asset holds NOW — MeshBakeStore's model cache was dropped by
    // the reimport that called us, so this reads the new blob.
    const iris::BakedModelPtr baked = MeshBakeStore::load(sourcePath, meshGuid);
    int swapped = 0;
    std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &node) {
        if (!node) return;
        if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            auto meshNode = node.staticCast<iris::MeshNode>();
            if (meshNode->meshPath == meshGuid) {
                iris::MeshPtr mesh;
                if (baked && meshNode->meshIndex >= 0
                    && meshNode->meshIndex < baked->meshes.size())
                    mesh = baked->meshes.at(meshNode->meshIndex);
                // The mirror re-attaches on a mesh POINTER change
                // (scenemirror.cpp), so nothing else has to be told.
                if (mesh) { meshNode->setMesh(mesh); ++swapped; }
            }
        }
        for (const iris::SceneNodePtr &child : node->children()) walk(child);
    };
    walk(live->getRootNode());
    return swapped;
}

void SceneEditService::addMesh(const QString &path, bool ignore, iris::Vec3 position)
{
    if (path.isEmpty()) return;

    // No SceneSource: the loader owns a local importer when none is passed
    // (the old `new` here leaked the whole parsed scene per added mesh).
    auto node = iris::MeshNode::loadAsSceneFragment(path, [](iris::MeshPtr mesh, iris::MeshMaterialData& data)
    {
        return iris::MaterialPtr(BuiltinMaterials::fromMeshData(data));
    });

    // model file may be invalid so null gets returned
    if (!node) return;

    // rename animation sources to relative paths
    auto relPath = QDir(project->folderPath).relativeFilePath(path);
    for (auto anim : node->getAnimations()) {
        if (!!anim->skeletalAnimation)
            anim->skeletalAnimation->source = relPath;
    }

    node->setLocalPos(position);

    // todo: load material data
    addNodeToScene(node, ignore);
}

void SceneEditService::addMaterialMesh(const QString &path, bool ignore, iris::Vec3 position,
                                       const QString &guid, const QString &assetName,
                                       surfaceplacement::Placement placement)
{
    Q_UNUSED(path);
    Q_UNUSED(assetName);
    auto document = QJsonDocument::fromJson(db->fetchAssetData(guid)).object();

    auto reader = new SceneReader;
    reader->setDatabaseHandle(db);
    reader->setProject(project);
    reader->setLibrarySource();
    iris::SceneNodePtr node = reader->readSceneNode(document);
    delete reader;
    // The reader returns null for a blob whose root is a node type this build
    // retired (sceneformat::isRetiredNodeType) — the other three readSceneNode
    // call sites already checked; this one dereferenced it.
    if (!node) return;

    // FRESH IDENTITY per instantiation. The blob stores the guids it was
    // authored with, so instantiating the same asset twice produced two live
    // subtrees SHARING every guid — Scene::nodes (guid-keyed), findNodeByGuid,
    // sockets and undo all break on the second copy (found by the Stage 3
    // lane: avatar.spawn of a second character collided with the first).
    // Same remap the paste path (insertFragment) uses.
    regenerateGuids(node);

    // Animation sources: point the model's OWN clips at the model's file, and
    // leave everybody else's alone.
    //
    // This used to rewrite EVERY skeletal clip on the node to the model file,
    // unconditionally (AVATAR_MODULE_SPEC §A.9). That is right for the clips
    // that came out of the model itself — their stored source is whatever path
    // the author's machine had — and wrong for every clip loaded from another
    // file: an avatar with a Walking.fbx clip had that clip re-sourced to the
    // character on the next instantiation, so on reopen it resolved to the
    // character's own animation and the character stopped walking.
    //
    // A clip belongs to this model when its source names the same FILE. Since
    // the CAS a stored object's file name is its sha256, so the comparison is
    // on the base name the writer recorded, not on the resolved path.
    const QString meshGuid = db->fetchObjectMesh(guid, static_cast<int>(ModelTypes::Object),
                                                 static_cast<int>(ModelTypes::Mesh));
    const QString modelName = db->fetchAsset(meshGuid).name;
    const QString relPath = QDir(project->folderPath).relativeFilePath(modelName);
    const QString modelFile = QFileInfo(modelName).fileName();
    for (auto anim : node->getAnimations()) {
        if (!anim->skeletalAnimation) continue;
        const QString source = anim->skeletalAnimation->source;
        // Empty (a clip the blob never sourced) or this model's own file.
        if (source.isEmpty() || QFileInfo(source).fileName() == modelFile
            || QFileInfo(source).fileName() == QFileInfo(relPath).fileName())
            anim->skeletalAnimation->source = relPath;
    }

    // Honour the drop position (the viewport computed where the cursor hit the
    // scene) — legacy addMesh does the same; without this every dropped asset
    // landed at the asset's authored origin (ASSET_ADD_AUDIT D1) — and REST it
    // on that point when the position came from a drop (owner, 2026-09-14: a
    // model whose pivot is its centre was buried to the waist in the floor).
    // The asset's size is BAKED (SPECS/IMPORT_DIALOG_SPEC.md §6), so the
    // subtree this measures is already the size the user will see and the node
    // is placed at scale 1. A model with a base pivot is not lifted
    // at all, so nothing is lifted twice (services/surfaceplacement.h).
    surfaceplacement::place(node, position, placement);

    addNodeToScene(node, ignore);
}

iris::MeshNodePtr SceneEditService::addImagePlane(const QString &textureGuid,
                                                  iris::Vec3 position,
                                                  const ImagePlaneOptions &opts)
{
    // Material first — ImageMaterial::fromTexture is the shared builder
    // (IMAGE_PLANE_SPEC §3): it resolves the bytes pin-first and probes the
    // alpha channel. No material means no resolvable image: refuse cleanly.
    QString resolvedPath;
    auto material = ImageMaterial::fromTexture(textureGuid, db, project, &resolvedPath);
    if (!material) {
        qWarning() << "addImagePlane: texture" << textureGuid << "resolves to no readable image";
        return iris::MeshNodePtr();
    }

    // Aspect from the import-time metadata block (lazy backfill for old
    // rows); a header-only decode is the fallback for rows with no block.
    const QJsonObject meta = AssetMetadata::ensure(db, textureGuid);
    int w = meta.value("width").toInt();
    int h = meta.value("height").toInt();
    if (w <= 0 || h <= 0) {
        const QSize size = QImageReader(resolvedPath).size();
        w = size.width();
        h = size.height();
    }
    if (w <= 0 || h <= 0) w = h = 1;

    const auto record = db->fetchAsset(textureGuid);
    const QString baseName = QFileInfo(record.name).completeBaseName();
    const QString nodeGuid = GUIDManager::generateGUID();

    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(
        ":/content/primitives/plane.obj",
        baseName.isEmpty() ? QStringLiteral("Image Plane") : baseName,
        nodeGuid, db);

    // plane.obj is a 2x2 XZ quad — 0.5 * the aspect-normalized extents caps
    // the long side at exactly 1 m.
    const float wf = static_cast<float>(w), hf = static_cast<float>(h);
    node->setLocalScale(0.5f * iris::Vec3(w >= h ? 1.0f : wf / hf,
                                         1.0f,
                                         h > w ? 1.0f : hf / wf));

    // Billboard-once (§5, owner call §8.4): rotate the +Y plane normal onto
    // the direction to the editor camera, roll aligned to the camera's up.
    // Afterwards the node is completely ordinary.
    if (viewport && viewport->editorCamera()) {
        auto cam = viewport->editorCamera();
        const iris::Vec3 toCam = (cam->getGlobalPosition() - position).normalized();
        if (!toCam.isNull()) {
            iris::Vec3 upHint = cam->getGlobalRotation().rotatedVector(iris::Vec3(0, 1, 0));
            iris::Vec3 right = iris::Vec3::crossProduct(upHint, toCam);
            if (right.lengthSquared() < 1e-6f)  // camera straight above/below
                right = iris::Vec3::crossProduct(iris::Vec3(0, 0, -1), toCam);
            right.normalize();
            // Local +X → right, +Y (the normal) → toCam, +Z → right × toCam
            // (the plane's V axis runs along +Z, so the image top faces the
            // camera's up).
            const iris::Vec3 zAxis = iris::Vec3::crossProduct(right, toCam);
            const float m[9] = { right.x(), toCam.x(), zAxis.x(),
                                 right.y(), toCam.y(), zAxis.y(),
                                 right.z(), toCam.z(), zAxis.z() };
            node->setLocalRot(iris::Quat::fromRotationMatrix(iris::Mat3(m)));
        }
    }

    node->setMaterial(material);
    if (!opts.doubleSided) {
        // createBasicMeshNode defaults to cull-none (double-sided) — exactly
        // the §8.3 default for image planes; single-sided culls back faces.
        node->setFaceCullingMode(iris::FaceCullingMode::Back);
    }

    // A DB object row like the built-in primitives get (the dependency row
    // below and the export walkers hang off it).
    QJsonObject props;
    props["type"] = "builtin";
    db->createAssetEntry(nodeGuid, node->getName(),
                         static_cast<int>(ModelTypes::Object),
                         project->getProjectGuid(), project->getProjectGuid(),
                         QString(), QString(), QByteArray(),
                         QJsonDocument(props).toJson(), QByteArray(), QByteArray());
    db->createDependency(static_cast<int>(ModelTypes::Object),
                         static_cast<int>(ModelTypes::Texture),
                         nodeGuid, textureGuid, project->getProjectGuid());

    // Position is already decided (the drop point) — ignore the spawn offset.
    node->setLocalPos(position);
    addNodeToScene(node, /*ignore=*/true);
    return node;
}

iris::DecalNodePtr SceneEditService::addDecal(const QString &textureGuid,
                                              const DecalOptions &opts)
{
    if (!scene()) return iris::DecalNodePtr();

    auto node = iris::DecalNode::create();
    node->width  = std::max(0.001f, opts.width);
    node->height = std::max(0.001f, opts.height);
    node->depth  = std::max(0.001f, opts.depth);
    node->metalness = qBound(0.0f, opts.metalness, 1.0f);
    node->roughness = qBound(0.0f, opts.roughness, 1.0f);
    node->ignoreAlphaDiffuse = opts.ignoreAlphaDiffuse;
    node->setName(QStringLiteral("Decal"));

    // The decal node gets a DB object row like the built-in primitives, so the
    // Object->Texture dependency row below has something to hang off (the
    // export walkers and the packaging closure read it).
    const QString nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
    if (db && project && !project->getProjectGuid().isEmpty()) {
        QJsonObject props;
        props["type"] = "builtin";
        db->createAssetEntry(nodeGuid, node->getName(),
                             static_cast<int>(ModelTypes::Object),
                             project->getProjectGuid(), project->getProjectGuid(),
                             QString(), QString(), QByteArray(),
                             QJsonDocument(props).toJson(), QByteArray(), QByteArray());
        node->isBuiltIn = true;
    }

    if (!textureGuid.isEmpty()) {
        // Name the node after the image, like the image plane does.
        const QString baseName = QFileInfo(db ? db->fetchAsset(textureGuid).name : QString())
                                     .completeBaseName();
        if (!baseName.isEmpty()) node->setName(baseName);
        setDecalTexture(node, textureGuid);
    }

    if (opts.positionGiven) {
        node->setLocalPos(opts.position);
        addNodeToScene(node, /*ignore=*/true);
    } else {
        addNodeToScene(node);
    }
    return node;
}

bool SceneEditService::setDecalTexture(const iris::DecalNodePtr &decal, const QString &textureGuid)
{
    // The diffuse-kind alias. Every caller that predates the three-map verb
    // (the panel's image row, the asset-bin drop, node.setDecalTexture, addDecal)
    // keeps working unchanged.
    return setDecalMap(decal, DecalMapKind::Diffuse, textureGuid);
}

bool SceneEditService::setDecalMap(const iris::DecalNodePtr &decal, DecalMapKind kind,
                                   const QString &textureGuid)
{
    if (!decal) return false;

    QString *guidField = nullptr;
    QString *pathField = nullptr;
    switch (kind) {
    case DecalMapKind::Diffuse:
        guidField = &decal->textureGuid;  pathField = &decal->resolvedTexturePath;  break;
    case DecalMapKind::Normal:
        guidField = &decal->normalGuid;   pathField = &decal->resolvedNormalPath;   break;
    case DecalMapKind::Emissive:
        guidField = &decal->emissiveGuid; pathField = &decal->resolvedEmissivePath; break;
    }

    const QString oldGuid = *guidField;
    *guidField = textureGuid;
    pathField->clear();

    // A dependency row is (node, asset) — not (node, asset, slot). Two kinds
    // bound to the SAME image share one row, so the old guid's row may only be
    // dropped once no other kind still holds it.
    const auto stillBound = [&decal](const QString &guid) {
        return !guid.isEmpty() && (decal->textureGuid == guid ||
                                   decal->normalGuid == guid ||
                                   decal->emissiveGuid == guid);
    };

    if (textureGuid.isEmpty()) {
        if (db && project && !oldGuid.isEmpty() && !project->getProjectGuid().isEmpty() &&
            !stillBound(oldGuid))
            db->deleteDependency(decal->getGUID(), oldGuid);
        return true;
    }
    if (!db || !project || project->getProjectGuid().isEmpty()) return false;

    // BINDING membership, never a direct add: a decal REFERS to an existing
    // image, so no companion PBR material is minted for it (the shared rule the
    // lights lane landed — LIGHTS_COMPLETION_SPEC D4 / ProjectAssets::AddKind).
    ProjectAssets::addToProject(textureGuid, db, project, ProjectAssets::AddKind::Binding);
    if (!oldGuid.isEmpty() && oldGuid != textureGuid && !stillBound(oldGuid))
        db->deleteDependency(decal->getGUID(), oldGuid);
    // Delete-then-create: `dependencies` has no unique key on (depender,
    // dependee), so re-binding the same image (or binding it to a second kind)
    // would otherwise stack duplicate rows the export closure walks twice.
    db->deleteDependency(decal->getGUID(), textureGuid);
    db->createDependency(static_cast<int>(ModelTypes::Object),
                         static_cast<int>(ModelTypes::Texture),
                         decal->getGUID(), textureGuid, project->getProjectGuid());

    *pathField = AssetCas::resolvePinned(
        QSqlDatabase::database(), AssetStorePaths::root(),
        project->getProjectGuid(), textureGuid);
    return true;
}

bool SceneEditService::setParticleTexture(const iris::ParticleSystemNodePtr &emitter,
                                          const QString &textureGuid)
{
    if (!emitter) return false;
    if (textureGuid.isEmpty()) { emitter->texture.clear(); return true; }
    if (!db || !project || project->getProjectGuid().isEmpty()) return false;

    // BINDING membership, never a direct add: an emitter REFERS to an existing
    // image, so no companion PBR material is minted for it — the same rule the
    // decal and light-profile bindings follow.
    ProjectAssets::addToProject(textureGuid, db, project, ProjectAssets::AddKind::Binding);
    db->removeDependenciesByType(emitter->getGUID(), ModelTypes::Texture);
    db->createDependency(static_cast<int>(ModelTypes::ParticleSystem),
                         static_cast<int>(ModelTypes::Texture),
                         emitter->getGUID(), textureGuid, project->getProjectGuid());

    const QString path = AssetCas::resolvePinned(
        QSqlDatabase::database(), AssetStorePaths::root(),
        project->getProjectGuid(), textureGuid);
    if (path.isEmpty()) return false;
    emitter->setTexture(iris::Texture2D::load(path));
    return !!emitter->texture;
}

void SceneEditService::addAssetParticleSystem(bool ignore, iris::Vec3 position, QString guid,
                                              QString assetName)
{

    QJsonObject pDefs;
    QVector<Asset*>::const_iterator iterator = AssetManager::getAssets().constBegin();
    while (iterator != AssetManager::getAssets().constEnd()) {
        if ((*iterator)->assetGuid == guid) pDefs = (*iterator)->getValue().toJsonObject();
        ++iterator;
    }

    auto particleNode = iris::ParticleSystemNode::create();

    particleNode->setGUID(pDefs["guid"].toString());
    particleNode->setPPS((float) pDefs["particlesPerSecond"].toDouble(1.0f));
    particleNode->setParticleScale((float) pDefs["particleScale"].toDouble(1.0f));
    particleNode->setDissipation(pDefs["dissipate"].toBool());
    particleNode->setDissipationInv(pDefs["dissipateInv"].toBool());
    particleNode->setRandomRotation(pDefs["randomRotation"].toBool());
    particleNode->setGravity((float) pDefs["gravityComplement"].toDouble(1.0f));
    particleNode->setBlendMode(pDefs["blendMode"].toBool());
    particleNode->setLife((float) pDefs["lifeLength"].toDouble(1.0f));
    particleNode->setName(pDefs["name"].toString());
    particleNode->setSpeed((float) pDefs["speed"].toDouble(1.0f));
    {
        // The stored value is an asset guid: resolve it through the CAS (the
        // pinned bytes in project context, else the library source). The flat
        // projectFolder/name join that followed is gone (plan item 15c):
        // nothing writes asset files into a project folder any more.
        auto textureGuid = pDefs["texture"].toString();
        if (!textureGuid.isEmpty()) {
            const QString texPath = AssetCas::resolvePinned(
                QSqlDatabase::database(), AssetStorePaths::root(),
                project->getProjectGuid(), textureGuid);
            if (!texPath.isEmpty()) particleNode->setTexture(iris::Texture2D::load(texPath));
        }
    }
    particleNode->setVisible(pDefs["visible"].toBool(true));

    particleNode->setPickable(true);
    particleNode->setGUID(guid);
    particleNode->setName(assetName);
    particleNode->setLocalPos(position);

    addNodeToScene(particleNode, ignore);
}

void SceneEditService::addNodeToActiveNode(iris::SceneNodePtr sceneNode)
{
    auto scene = this->scene();
    if (!scene) {
        //todo: set alert that a scene needs to be set before this can be done
    }

    // apply default material
    if (sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();

        if (!meshNode->getMaterial()) {
            // The engine viewport authors PBR only.
            meshNode->setMaterial(iris::PbrMaterial::create());
        }
    }

    if (auto activeSceneNode = selection->selected()) {
        activeSceneNode->addChild(sceneNode);
    } else {
        scene->getRootNode()->addChild(sceneNode);
    }
    // SCENE_STATIC (SCENEGRAPH_SPEC §6), after the node is in the tree — a
    // node's static class depends on its parent's, so this cannot be decided
    // before it has one. Moving it later takes it back out, automatically.
    sceneNode->applyStaticDefaults();

    emit hierarchyChanged();
}

iris::Vec3 SceneEditService::freeSpotNear(const iris::Vec3 &spot, const iris::Vec3 &step) const
{
    auto scene = this->scene();
    if (!scene || !scene->getRootNode()) return spot;
    // 1 m apart is "not the same body" at the 1 u = 1 m scene scale, and the
    // stride is wider than the gap so the second try always clears the first.
    const float kMinSeparation = 1.0f;
    const float kStride = 1.5f;
    iris::Vec3 side = step;
    if (side.length() < 0.0001f) side = iris::Vec3(1, 0, 0);
    side = side.normalized();

    iris::Vec3 candidate = spot;
    // Bounded: eight tries is a row of characters wide enough that the ninth
    // add landing on the eighth is nobody's real workflow — and an unbounded
    // search with a full scene would walk forever.
    for (int attempt = 0; attempt < 8; ++attempt) {
        bool taken = false;
        for (const auto &child : scene->getRootNode()->children()) {
            if (!child) continue;
            if ((child->getLocalPos() - candidate).length() < kMinSeparation) { taken = true; break; }
        }
        if (!taken) return candidate;
        candidate = candidate + side * (kStride * float(attempt + 1));
    }
    return candidate;
}

void SceneEditService::addNodeToScene(iris::SceneNodePtr sceneNode, bool ignore)
{
    auto scene = this->scene();
    if (!scene) {
        // @TODO: set alert that a scene needs to be set before this can be done
        return;
    }

    // @TODO: add this to a constants file
    if (!ignore) {
        const float spawnDist = 10.0f;
        auto camera = viewport->editorCamera();
        auto offset = camera->getLocalRot().rotatedVector(iris::Vec3(0, -1.0f, -spawnDist));
        offset += camera->getLocalPos();
        // NEVER STACKED (S9, owner report: "adding one part puts 3-4 copies in
        // the scene"). An add with no position lands in front of the camera —
        // and a SECOND one, from a camera that has not moved, landed at the
        // exact same point, so two characters occupied one body. Step sideways
        // (camera right) until the spot is free.
        sceneNode->setLocalPos(freeSpotNear(offset, camera->getLocalRot().rotatedVector(
                                                        iris::Vec3(1, 0, 0))));
    }

    // apply default material to mesh nodes if there is none
    if (sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();
        if (!meshNode->getMaterial()) {
            // The engine viewport authors PBR only.
            meshNode->setMaterial(iris::PbrMaterial::create());
        }
    }

    auto cmd = new AddSceneNodeCommand(scene->getRootNode(), sceneNode);
    undo->push(cmd);
}

bool SceneEditService::deleteNode(iris::SceneNodePtr node)
{
    if (!node) return false;
    // TODO - do a deps check here as well
    // TODO - gray/disable delete button if a node isn't removable
    if (node->isRootNode() || !node->isRemovable()) return false;

    // The command owns the asset-row cleanup: the row is deleted only when the
    // delete becomes permanent, so undo no longer resurrects a node whose DB
    // asset is gone (SCRIPTING_SPEC §1.2).
    auto cmd = new DeleteSceneNodeCommand(node->getParent(), node,
                                          node->isBuiltIn ? db : nullptr, node->getGUID());
    undo->push(cmd);
    return true;
}

iris::SceneNodePtr SceneEditService::duplicateNode(iris::SceneNodePtr source)
{
    if (!scene()) return iris::SceneNodePtr();
    if (!source || !source->isDuplicable()) return iris::SceneNodePtr();

    auto node = source->duplicate();
    // THE COPY GETS ITS OWN NAME (owner decision 2026-09-09, the Unreal rule):
    // "Cube" -> "Cube2" -> "Cube3", a numeric suffix and no space, unique among
    // the siblings it is about to join. iris::SceneNode::duplicate keeps the
    // name verbatim, which left a scene full of nodes called "Cube" that the
    // outliner, node.find and every verb that names one could not tell apart.
    // The rename happens BEFORE the add command so the command's own text
    // ("Add Cube2") and the outliner row agree from the first frame.
    node->setName(nodenaming::uniqueSiblingName(source->getParent(), source->getName()));
    // Undoable now (SCRIPTING_SPEC §1.2): the add command parents the copy,
    // refreshes the hierarchy and selects it — the manual addChild+repopulate
    // this slot used to do, minus the missing undo entry.
    //
    // RIGHT AFTER THE ORIGINAL, not at the end of the parent's children (undo
    // v1.5): a duplicate that jumps to the bottom of a 200-row outliner is a
    // duplicate the user has to go and find. The index is also what makes the
    // undo/redo pair exact — AddSceneNodeCommand restores the slot it was told
    // about, so redo after undo puts the copy back beside its original instead
    // of appending it somewhere new.
    const int after = source->siblingIndex();
    undo->push(new AddSceneNodeCommand(source->getParent(), node,
                                       after >= 0 ? after + 1 : -1));
    return node;
}

QString SceneEditService::renameNode(const iris::SceneNodePtr &node, const QString &desired)
{
    // The edit gate (round 2, item 7). The push below is refused for a hand
    // edit, but this function ANSWERS with the name it would have given — and
    // the hierarchy reads that as success and keeps the typed row text. An
    // empty answer is what every other refusal here reads as. (blocked(), not
    // refuse(): the push that follows counts it and raises the notice.)
    if (editgate::blocked()) return QString();
    if (!node || node->isRootNode()) return QString();
    const QString wanted = desired.trimmed();
    if (wanted.isEmpty()) return QString();

    // Unique among the SIBLINGS, excluding the node itself: renaming "Cube2"
    // to "Cube2" is a no-op, not a request for "Cube3".
    const QString given = nodenaming::uniqueSiblingName(node->getParent(), wanted, node.data());
    const QString before = node->getName();
    if (given == before) return given;

    // The outliner rebuilds from the document on hierarchyChanged — the same
    // refresh a reparent raises — so undo and redo put the row text back too,
    // not only the name. A QPointer rather than a raw `this`: the command sits
    // on the undo stack, and this service is the one thing it captures without
    // owning (the stack is cleared on project close; the service lives with
    // the window).
    QPointer<SceneEditService> self(this);
    const iris::SceneNodePtr target = node;
    auto apply = [self, target](const QString &name) {
        target->setName(name);
        if (self) self->notifyHierarchyChanged();
    };
    undo->push(new NodeEditCommand(
        QStringLiteral("Rename %1 to %2").arg(before, given),
        [apply, given]() { apply(given); },
        [apply, before]() { apply(before); }));
    return given;
}

SceneFragment SceneEditService::captureFragment(const iris::SceneNodePtr &node) const
{
    if (!node) return SceneFragment();
    SceneWriter::setProject(project);
    return SceneWriter::captureFragment(node);
}

iris::SceneNodePtr SceneEditService::rebuildFragment(const SceneFragment &fragment) const
{
    if (fragment.isNull()) return iris::SceneNodePtr();
    // A reader per rebuild. It is not free (its mesh cache is per instance), but
    // a long-lived one would hold every mesh a rebuild ever touched for the life
    // of the service, and a rebuild is a user-scale event — an undo, a paste —
    // not a loop.
    SceneReader reader;
    reader.setDatabaseHandle(db);
    reader.setProject(project);
    reader.setLibrarySource();
    return reader.readFragment(fragment);
}

namespace {

/// Gives every node of a rebuilt subtree a FRESH guid, and re-points the
/// references inside the subtree that named the old ones.
///
/// A paste is a COPY, and two live nodes on one guid is not a copy — it is a
/// document that cannot be addressed (the scripting layer resolves by guid, the
/// active-camera field is a guid, socket owners are guids, and the mesh-bake
/// rows are keyed on them). The shipped Particles sample carried exactly that
/// defect for years, from an add path that reused an asset's guid as a node's,
/// and it had to be re-authored to fix it.
///
/// Same rule and THE SAME REMAP as iris::SceneNode::duplicate — literally the
/// same function since CLIPBOARD_SPEC §3.2: a reference INSIDE the copied
/// subtree becomes the copy's own, a reference outside it keeps its guid (the
/// "second camera on the same character" case). It covers socket owners,
/// physics constraint endpoints and a camera's focus target; the last two were
/// the recorded gap, open on BOTH paths, and closing it in the document meant
/// Duplicate and Paste could not drift apart afterwards.
void regenerateGuids(const iris::SceneNodePtr &root, QHash<QString, QString> *guidMapOut)
{
    QHash<QString, QString> guidMap;
    std::function<void(const iris::SceneNodePtr &)> assign = [&](const iris::SceneNodePtr &n) {
        const QString fresh = IrisUtils::generateGUID();
        guidMap.insert(n->getGUID(), fresh);
        n->setGUID(fresh);
        const int kids = n->childCount();
        for (int i = 0; i < kids; ++i)
            if (iris::SceneNode *c = n->childAt(i)) assign(c->sharedFromThis());
    };
    assign(root);
    root->remapNodeReferences(guidMap);
    if (guidMapOut) guidMapOut->insert(guidMap);
}

} // namespace

iris::SceneNodePtr SceneEditService::insertFragment(const SceneFragment &fragment,
                                                   iris::SceneNodePtr parent,
                                                   int index,
                                                   QHash<QString, QString> *guidMapOut)
{
    auto sc = scene();
    if (!sc) return iris::SceneNodePtr();
    auto node = rebuildFragment(fragment);
    if (!node) return iris::SceneNodePtr();
    // A PASTE, not a restore: fresh identity. (Undo does not come through here
    // — it calls rebuildFragment directly, because restoring a deleted node
    // must give back the guid the rest of the document still refers to.)
    regenerateGuids(node, guidMapOut);
    // A pasted floor is a copy, and a scene has ONE default floor
    // (services/defaultfloor.h): the copy — and anything under it — is an
    // ordinary mesh, as Duplicate makes it. (Here and not in rebuildFragment:
    // an undo of a delete must give the floor back AS the floor.)
    std::function<void(const iris::SceneNodePtr &)> clearFloor =
        [&clearFloor](const iris::SceneNodePtr &n) {
            if (n->getSceneNodeType() == iris::SceneNodeType::Mesh)
            {
                const iris::MeshNodePtr floor = n.staticCast<iris::MeshNode>();
                floor->defaultFloor = false;
                // The mirror reads `defaultFloor` to find the horizon plate's
                // subject; a hand-written reflected field has to say so.
                floor->markChanged(iris::NodeChange::Params);
            }
            for (const auto &child : n->children()) clearFloor(child);
        };
    clearFloor(node);
    if (!parent) parent = sc->getRootNode();
    // ...and a fresh NAME when the one it carries is already taken under that
    // parent — the same rule Duplicate uses, because a paste is a copy too
    // (owner decision 2026-09-09). Pasting into a parent that has no node of
    // this name keeps the name: the suffix disambiguates, it does not brand.
    node->setName(nodenaming::uniqueSiblingName(parent, node->getName()));
    // The SAME add command every other add uses — one undo entry, the hierarchy
    // refresh, the selection, and the SCENE_STATIC pass on redo.
    auto *cmd = new AddSceneNodeCommand(parent, node, index);
    cmd->setText(QObject::tr("Paste %1").arg(node->getName()));
    undo->push(cmd);
    return node;
}

// ---- the selection SET (EDITOR_MULTISELECT_SPEC §2.5) ----------------------

QList<iris::SceneNodePtr> SceneEditService::effectiveSet(const QList<iris::SceneNodePtr> &nodes)
{
    QList<iris::SceneNodePtr> out;
    for (const auto &node : nodes) {
        if (!node) continue;
        bool ancestorSelected = false;
        for (auto p = node->getParent(); !!p; p = p->getParent()) {
            for (const auto &other : nodes)
                if (!!other && other.data() == p.data()) { ancestorSelected = true; break; }
            if (ancestorSelected) break;
        }
        if (ancestorSelected) continue;
        bool seen = false;
        for (const auto &o : out) if (o.data() == node.data()) { seen = true; break; }
        if (!seen) out.append(node);
    }
    return SelectionService::sortDocumentOrder(out);
}

SceneEditService::DeleteSetResult
SceneEditService::deleteNodes(const QList<iris::SceneNodePtr> &nodes)
{
    DeleteSetResult result;
    if (!scene()) return result;
    // THE EDIT GATE (owner, ledger §423): asked ONCE, at the gesture's entry,
    // for the same reason the multi-node branch below opens a macro before its
    // first command — refusing the commands one at a time would leave an EMPTY
    // macro on the stack, which is an undo entry that eats the user's next
    // Ctrl+Z. Keyed on the calling context, so the verb that shares this
    // funnel is not gated.
    if (editgate::refuse()) return result;

    // THE WORLD ROOT COMES OUT FIRST, before the D5 reduction (D6): every other
    // node in the scene is its descendant, so reducing with the root still in
    // the list would drop the whole selection and delete nothing. It is
    // reported as skipped, not refused for everyone.
    QList<iris::SceneNodePtr> input;
    for (const auto &node : nodes) {
        if (!node) continue;
        if (node->isRootNode()) { result.skipped.append(node->getGUID()); continue; }
        input.append(node);
    }

    QList<iris::SceneNodePtr> targets;
    for (const auto &node : effectiveSet(input)) {
        // A node the document marks non-removable is skipped and reported too.
        if (!node->isRemovable()) { result.skipped.append(node->getGUID()); continue; }
        targets.append(node);
    }
    if (targets.isEmpty()) return result;

    // LAST FIRST. Every DeleteSceneNodeCommand captures its node's sibling
    // index for its own undo; deleting an earlier sibling first would shift
    // every later one, so the captured indices would restore the subtree in the
    // wrong slot. effectiveSet() hands back document order, so reversing it is
    // exactly "last sibling first, deepest last".
    std::reverse(targets.begin(), targets.end());

    const bool macro = targets.size() > 1 && undo && undo->stack();
    if (macro) undo->stack()->beginMacro(QObject::tr("Delete %1 objects").arg(targets.size()));
    for (const auto &node : targets) {
        const QString guid = node->getGUID();
        if (deleteNode(node)) result.deleted.append(guid);
        else                  result.skipped.append(guid);
    }
    if (macro) undo->stack()->endMacro();

    // The delete commands each re-selected (single) as they always have; the
    // set ends EMPTY, which is what deleting what you had selected means.
    if (selection) selection->clear();
    return result;
}

QList<iris::SceneNodePtr> SceneEditService::selectAll()
{
    QList<iris::SceneNodePtr> all;
    auto sc = scene();
    if (!sc || !selection) return all;
    auto root = sc->getRootNode();
    if (!root) return all;
    std::function<void(const iris::SceneNodePtr &)> walk = [&](const iris::SceneNodePtr &n) {
        for (const auto &child : n->children()) {
            if (!child) continue;
            all.append(child);
            walk(child);
        }
    };
    walk(root);                       // the root itself never joins (D6)
    if (all.isEmpty()) { selection->clear(); return all; }
    // Document pre-order already: the walk IS the writer's walk. The first
    // entry is therefore the topmost outliner row, which becomes the primary.
    selection->select(all);
    return selection->selectedSet();
}

QList<iris::SceneNodePtr>
SceneEditService::duplicateNodes(const QList<iris::SceneNodePtr> &nodes)
{
    QList<iris::SceneNodePtr> copies;
    if (!scene()) return copies;
    if (editgate::refuse()) return copies;      // the edit gate, as in deleteNodes

    QList<iris::SceneNodePtr> input;      // D6, same reason as deleteNodes
    for (const auto &node : nodes) if (!!node && !node->isRootNode()) input.append(node);
    QList<iris::SceneNodePtr> targets = effectiveSet(input);
    if (targets.isEmpty()) return copies;

    // The primary's copy has to become the new primary, so remember which
    // original it was before the order is reversed.
    iris::SceneNodePtr primarySource;
    for (const auto &n : nodes) {
        if (!n) continue;
        for (const auto &t : targets) if (t.data() == n.data()) { primarySource = t; break; }
        if (primarySource) break;
    }

    // Same reason as deleteNodes: each copy lands at its original's sibling
    // index + 1, and doing the earlier siblings first would move the later
    // originals out from under their own index.
    std::reverse(targets.begin(), targets.end());

    const bool macro = targets.size() > 1 && undo && undo->stack();
    if (macro) undo->stack()->beginMacro(QObject::tr("Duplicate %1 objects").arg(targets.size()));
    QList<QPair<iris::SceneNodePtr, iris::SceneNodePtr>> made;   // source -> copy
    for (const auto &node : targets) {
        auto copy = duplicateNode(node);
        if (copy) made.append({ node, copy });
    }
    if (macro) undo->stack()->endMacro();

    // Back to document order, primary's copy first.
    iris::SceneNodePtr primaryCopy;
    QList<iris::SceneNodePtr> rest;
    for (const auto &pair : made) {
        if (primarySource && pair.first.data() == primarySource.data()) primaryCopy = pair.second;
        else rest.append(pair.second);
    }
    if (primaryCopy) copies.append(primaryCopy);
    copies.append(SelectionService::sortDocumentOrder(rest));
    if (selection && !copies.isEmpty()) selection->select(copies);
    return copies;
}

namespace {

// Every mesh node at or under `node`. An imported model roots at an Empty
// container node, and the viewport's click-selects-the-root rule hands exactly
// that container to the material paths — a mesh-only guard silently dropped
// the apply on the floor (the "PBR materials lost on reopen" data loss: the
// materials never entered the document, so the writer had nothing to save).
/// The scene node with this guid, or null. (One local walk: the scripting
/// modules' findNodeByGuid lives in their own shared header and this file may
/// not include it.)
iris::SceneNodePtr findSceneNodeByGuid(const iris::SceneNodePtr &node, const QString &guid)
{
    if (!node) return iris::SceneNodePtr();
    if (node->getGUID() == guid) return node;
    for (const auto &child : node->children()) {
        auto hit = findSceneNodeByGuid(child, guid);
        if (hit) return hit;
    }
    return iris::SceneNodePtr();
}

void collectMeshNodes(const iris::SceneNodePtr &node, QList<iris::MeshNodePtr> &out)
{
    if (!node) return;
    if (node->sceneNodeType == iris::SceneNodeType::Mesh)
        out.append(node.staticCast<iris::MeshNode>());
    for (const auto &child : node->children()) collectMeshNodes(child, out);
}

} // namespace

iris::MaterialPtr SceneEditService::resolveMaterial(const QString &presetOrGuid) const
{
    if (presetOrGuid.isEmpty()) return iris::MaterialPtr();

    bool isPreset = false;
    const MaterialPreset preset = MaterialPresets::find(presetOrGuid, &isPreset);
    // …UNLESS THIS PROJECT HAS ITS OWN COPY OF THAT PRESET (PRESET-EDIT-1):
    // the hover preview must show what the drop will apply, and the drop
    // applies the copy (see applyMaterial). Read through the ordinary bundle
    // branch below, which is the copy's own definition.
    const QString mine = isPreset
                             ? presetedit::projectCopyOf(
                                   db, project, MaterialPresetAssets::guidFor(presetOrGuid))
                             : QString();
    if (isPreset && mine.isEmpty()) return BuiltinMaterials::fromPreset(preset);

    if (!db) return iris::MaterialPtr();
    const QString wanted = mine.isEmpty() ? presetOrGuid : mine;

    // A MATERIAL row: its stored definition, dispatched on the materialType the
    // writer stamps. This is the same call applyMaterialAsset makes, which is
    // the whole point — what the hover shows and what the drop commits cannot
    // be two different readings of one row.
    MaterialReader reader;
    reader.setProject(project);
    const AssetRecord row = db->fetchAsset(wanted);
    if (row.type == static_cast<int>(ModelTypes::Material)) {
        // THE BUNDLE'S DEFINITION, pin-first (MATERIAL_BUNDLE_SPEC D-2/F11):
        // a project renders the version it was built with, not whatever the
        // library row holds now. Falls back to the row blob for a material
        // that has no stored definition yet.
        const QJsonObject matObject = MaterialBundle::read(db, wanted, project);
        if (matObject.isEmpty()) return iris::MaterialPtr();
        return reader.parseMaterialTyped(matObject, db);
    }

    // (THE SHADER BRANCH IS GONE — MATERIAL_BUNDLE_SPEC phase 2's Deletes
    // column. A ModelTypes::Shader row was the Materials module's separate
    // graph asset; there is one MATERIAL row now, with the graph as a payload
    // of its definition, and nothing in the app can mint the old kind.)
    return iris::MaterialPtr();
}

bool SceneEditService::applyMaterial(const QString &presetOrGuid, iris::SceneNodePtr target)
{
    if (presetOrGuid.isEmpty() || !target) return false;

    // A BORROWED MATERIAL MUST NOT BE THE ONE THE UNDO STEP CAPTURES. The hover
    // preview lends the mesh's slot; if it were still live here the command
    // would record the PREVIEW as the "original" and an undo would leave the
    // user with a material they never applied. Ending it first is also what
    // lets the mirror charge the commit — and only the commit — a GI re-solve.
    if (preview) preview->end();

    // NOTHING IS IMPORTED FOR AN APPLY THAT CANNOT HAPPEN (fix round F6). The
    // mesh-empty test and the edit gate live inside the two applies below, so
    // seeding first meant that double-clicking a preset with a LIGHT selected
    // — the tray passes the selection with no type check — imported three
    // PNGs, minted a row and paid for it, and then returned false. Both
    // questions are asked here, once, before anything is written down.
    {
        QList<iris::MeshNodePtr> meshes;
        collectMeshNodes(target, meshes);
        if (meshes.isEmpty()) return false;
    }
    if (editgate::refuse()) return false;

    // A PRESET IS A LIBRARY BUNDLE (phase 3). Seeded the first time anybody
    // uses it — with its reserved guid, its maps as member textures and its
    // definition in the store — and then applied like any other material
    // asset: a PIN and a document edit. That is what makes three applies of
    // "Gold PBR" produce ZERO new rows where they used to mint one each, and
    // what lets an undo take the membership back with the material.
    //
    // THE BYTES ARE USUALLY ALREADY IN THE STORE when this runs: the seeder
    // (services/materialpresetseeder.h) puts every preset's maps there on a
    // worker at launch, so the import here takes `AssetCas::storeObject`'s
    // dedup branch and writes no bytes and no fsync. A drop that beats the
    // seeder still works — it just pays for its own maps, as it did before
    // the seeder existed.
    QString materialGuid = presetOrGuid;
    if (MaterialPresetAssets::isPreset(presetOrGuid)) {
        // THIS PROJECT'S OWN COPY OF IT, IF IT HAS ONE (PRESET-EDIT-1). Once a
        // project has edited "Wood PBR", that name means the project's copy —
        // dropping the shipped tile again must not put a SECOND material of
        // the same name in the tray beside the user's own, and must not paint
        // the mesh with a picture they have already changed.
        const QString mine = presetedit::projectCopyOf(
            db, project, MaterialPresetAssets::guidFor(presetOrGuid));
        if (!mine.isEmpty()) return applyMaterialAsset(mine, target);
        // ONE IMPORTER AT A TIME (see MaterialPresetSeeder::finishNow): a
        // drop that beats the launch seed takes the job over rather than
        // racing it into two Texture rows for one picture.
        MaterialPresetSeeder::instance().finishNow();
        QString error;
        materialGuid = MaterialPresetAssets::ensureSeeded(presetOrGuid, db, &error);
        if (materialGuid.isEmpty()) {
            // A SESSION WITH NO LIBRARY, or a preset whose maps this build
            // does not ship: the material is still applied, from the shipped
            // values, because refusing to paint the mesh would be the worse
            // answer — there is simply no row to pin.
            irisLog("applyMaterial: '" + presetOrGuid + "' was not seeded - " + error);
            return applyResolvedMaterial(presetOrGuid, target);
        }
    }

    if (!db) return false;
    // ONE APPLY, because there is one kind of material row (phase 2's
    // Deletes: `applyMaterialShader`, the second dispatcher for the module's
    // old graph asset, is gone with the rows it served).
    return applyMaterialAsset(materialGuid, target);
}

bool SceneEditService::applyResolvedMaterial(const QString &presetOrGuid,
                                             iris::SceneNodePtr target)
{
    // THE LIBRARY-LESS FALLBACK, and the only apply that stores nothing: the
    // material is built from what `resolveMaterial` reads and pushed per mesh.
    // A fresh instance each, because `MeshNode::setMaterial` mutates what it
    // is handed.
    QList<iris::MeshNodePtr> meshes;
    collectMeshNodes(target, meshes);
    if (meshes.isEmpty()) return false;
    if (editgate::refuse()) return false;

    iris::MaterialPtr probe = resolveMaterial(presetOrGuid);
    if (!probe) return false;

    undo->stack()->beginMacro(QObject::tr("Apply Material"));
    for (const auto &meshNode : meshes) {
        auto mat = resolveMaterial(presetOrGuid);
        if (!mat) continue;
        undo->push(new ChangeMaterialCommand(meshNode, mat));
    }
    undo->stack()->endMacro();
    emit materialApplied(QStringLiteral("PBR"));
    return true;
}

// (THE ROW-PER-APPLY TAIL IS GONE — MATERIAL_BUNDLE_SPEC phase 3's Deletes
// column, the materials audit's F5.)
//
// Applying a preset used to mint PROJECT BOOKKEEPING: an "Presets" folder,
// a Material row carrying a copy of the preset's values (one per preset per
// project after PRESETS-1 — one per APPLY before it, which is why the
// owner's library held "Gold PBR" three times), a `matgen.material` file
// written only so a thumbnail request had a path to read, and hand-written
// Material->Texture and Object->Material dependency rows. None of it was in
// the undo stack, so an undone apply left every bit of it behind.
//
// A preset is a library bundle with a reserved guid now
// (services/materialpresetassets.h): the definition and the membership edges
// are the BUNDLE's, written once when it is seeded; the project's claim on
// it is a PIN, pushed as a command inside the apply's macro; and the
// thumbnail is the row's, rendered by the thumbnail service like every other
// material's. `applyMaterialAsset` below is the whole apply.


bool SceneEditService::applyMaterialAsset(const QString &assetGuid, iris::SceneNodePtr target)
{
    // A preview ends BEFORE a command is BUILT, not at its push: the command's
    // constructor is what captures "the original" (the read of the code review,
    // F1 — UndoService's pre-push hook runs after that and is only the backstop).
    if (preview) preview->end();
    if (editgate::refuse()) return false;       // the edit gate, as in deleteNodes
    QList<iris::MeshNodePtr> meshes;
    collectMeshNodes(target, meshes);
    if (meshes.isEmpty()) return false;

    const QJsonObject matObject = MaterialBundle::read(db, assetGuid, project);
    if (matObject.isEmpty()) return false;

    MaterialReader reader;
    reader.setProject(project);

    undo->stack()->beginMacro(QObject::tr("Apply Material"));

    // APPLYING IS A USE (lane L13): the project carries what its scene uses,
    // so a library material applied by guid is pinned in as a BINDING (a
    // tray/verb drop of a project member is already pinned — idempotent), and
    // the node -> material edge says who uses it.
    // Only a LIBRARY row (a project's own rows are members already — the
    // view filter tells them apart; the row's project_guid does not, an import
    // made with a project open records it) that the project does not pin yet:
    // addToProject re-pins to the library's CURRENT version, which would
    // silently upgrade an older pin.
    //
    // AND IT IS INSIDE THE MACRO NOW, as a COMMAND (the audit's F5, phase 3).
    // It was a bare service call after `endMacro`, so undoing an apply left
    // the pin — the material stayed in the project's tray with its textures,
    // and nothing the user could press took it back. Pushed FIRST so the
    // undo unwinds it LAST: the document stops wearing the material before
    // the project stops holding it.
    if (project && !project->getProjectGuid().isEmpty()) {
        const AssetRecord row = db->fetchAsset(assetGuid);
        const bool libraryRow = row.view_filter == AssetViewFilter::AssetsView
                                || row.view_filter == AssetViewFilter::Effects;
        if (libraryRow && !db->isAssetPinnedBy(project->getProjectGuid(), assetGuid))
            undo->push(new PinAssetCommand(db, project, assetGuid));
    }

    for (const auto &meshNode : meshes) {
        // Fresh instance per mesh; parseMaterialTyped dispatches on the stored
        // materialType, so a saved PBR material comes back as a real
        // PbrMaterial instead of a broken shader-less CustomMaterial.
        auto mat = reader.parseMaterialTyped(matObject, db);
        if (!mat) continue;
        undo->push(new ChangeMaterialCommand(meshNode, mat));
    }
    // THE USE EDGES, INSIDE THE MACRO AND AS COMMANDS (fix round F7). "This
    // mesh uses that material" was written after `endMacro` and nothing took
    // it back, so an undone apply left the catalog asserting a use that no
    // longer existed — the same leftover as the pin, one level down. With no
    // project open there is nobody to record it for (a preset applied in the
    // startup placeholder session reaches here now that presets are ordinary
    // material assets; it renders, and nothing is written down).
    if (project && !project->getProjectGuid().isEmpty()) {
        for (const auto &meshNode : meshes)
            undo->push(new MaterialUseEdgeCommand(db, project->getProjectGuid(),
                                                  meshNode->getGUID(), assetGuid));
    }

    undo->stack()->endMacro();

    emit materialApplied(matObject["materialType"].toString() == "pbr"
                             ? QStringLiteral("PBR")
                             : QStringLiteral("custom"));
    return true;
}

void SceneEditService::requestAssetViewRefresh()
{
    emit assetViewRefreshRequested();
}

void SceneEditService::forgetMaterialDressing(const QString &materialGuid)
{
    mDressedFrom.remove(materialGuid);
}

int SceneEditService::refreshMaterialUsers(const QString &materialGuid)
{
    if (materialGuid.isEmpty() || !db) return 0;
    // A BORROWED MATERIAL MUST NOT BE CLOBBERED (the rule its five siblings in
    // this file already follow). A refresh landing mid-hover would replace the
    // slot the preview lent, and the drag-leave restore would then put the
    // PREVIOUS material back — silently undoing the refresh. Ending the
    // preview first makes the restore a no-op.
    if (preview) preview->end();

    auto root = scene() ? scene()->getRootNode() : iris::SceneNodePtr();
    if (!root) return 0;
    const QString projectGuid = project ? project->getProjectGuid() : QString();

    // NOTHING CHANGED, NOTHING MOVES. This runs on the graph page's 1.5 s
    // autosave — while the user types — and handing a mesh a fresh material
    // POINTER is a re-attach to the mirror, which invalidates the GI caches
    // WHOLE (every cascade re-voxelises, in and out; ledger 804-805). The
    // content-addressed store makes the guard exact and free: a save that
    // produced the same definition produced the same OID, so comparing the
    // material's effective content id with the one this scene was last
    // dressed from answers "did anything change?" with no parse and no walk.
    QSqlDatabase conn = QSqlDatabase::database();
    const QString oid = projectGuid.isEmpty()
                            ? AssetCas::sourceOid(conn, materialGuid)
                            : AssetCas::pinnedOid(conn, projectGuid, materialGuid);
    const QString effective = oid.isEmpty() ? AssetCas::sourceOid(conn, materialGuid) : oid;
    if (!effective.isEmpty() && mDressedFrom.value(materialGuid) == effective) return 0;

    // THE DEFINITION IS READ ONCE. `resolveMaterial` is a store read plus a
    // JSON parse plus a texture resolve per call, and it used to be called
    // once PER MESH — ninety reads of one file to dress ninety meshes. The
    // read happens here; the INSTANCE is still built per mesh, because
    // `MeshNode::setMaterial` mutates what it is handed (SKINNING_ENABLED and
    // friends), so a shared instance across two meshes is a defect waiting
    // for a skinned one.
    const QJsonObject definition = MaterialBundle::read(db, materialGuid, project);
    if (definition.isEmpty()) return 0;
    MaterialReader reader;
    reader.setProject(project);

    int redressed = 0;
    for (const QString &nodeGuid : db->fetchDependers(materialGuid, projectGuid)) {
        auto node = findSceneNodeByGuid(root, nodeGuid);
        if (!node) continue;
        QList<iris::MeshNodePtr> meshes;
        collectMeshNodes(node, meshes);
        for (const auto &meshNode : meshes) {
            auto mat = reader.parseMaterialTyped(definition, db);
            if (!mat) continue;
            meshNode->setMaterial(mat);
            ++redressed;
        }
    }
    if (!effective.isEmpty()) mDressedFrom.insert(materialGuid, effective);
    return redressed;
}

bool SceneEditService::resetMaterial(iris::SceneNodePtr node)
{
    // A preview ends BEFORE a command is BUILT, not at its push: the command's
    // constructor is what captures "the original" (the read of the code review,
    // F1 — UndoService's pre-push hook runs after that and is only the backstop).
    if (preview) preview->end();
    if (!node || node->getSceneNodeType() != iris::SceneNodeType::Mesh) return false;
    // The edit gate (round 2, item 10), before the work: this verb PINS the
    // default material's textures into the project database on its way to the
    // push and emits materialApplied after it — both would have happened
    // around a command that was refused.
    if (editgate::refuse()) return false;
    if (!materialdefaults::hasDefault(node)) return false;
    QStringList defaultTextures, newlyPinned;
    auto material = materialdefaults::create(node, db, project, &defaultTextures, &newlyPinned);
    if (!material) return false;
    undo->push(new ResetMaterialCommand(db, project, node.staticCast<iris::MeshNode>(),
                                        material, defaultTextures, newlyPinned));
    emit materialApplied(QStringLiteral("PBR"));
    return true;
}

void SceneEditService::createMaterialFromNode(iris::SceneNodePtr node, const QString &folderGuid)
{
    if (!!node) {
        QJsonObject materialDef;
        // (nick) the material version gets updated during writing so
        // it's safe to assume we're working the v2 material structure
        SceneWriter::writeSceneNodeMaterial(
            materialDef,
            node.staticCast<iris::MeshNode>()->getMaterial()
        );

        // materialDef will be mutated
        // it's only used to generate a file for the thumbnail
        auto materialDefOriginal = materialDef;

        // replace material guid with texture name
        auto materialValues = materialDef["values"].toObject();
        for (const auto &key : materialValues.keys()) {
            if (materialValues[key].isString())
            {
                auto texName = db->fetchAsset(materialValues[key].toString()).name;
                if (texName.isEmpty())
                    continue;
                materialValues[key] = texName;
            }
        }
        materialDef["values"] = materialValues;

        QJsonDocument saveDoc;
        //saveDoc.setObject(materialDef);
        saveDoc.setObject(materialDefOriginal);

        QString fileName = IrisUtils::join(
            project->getProjectFolder(),
            IrisUtils::buildFileName(node.staticCast<iris::MeshNode>()->getName(), "material")
        );

        QFile file(fileName);
        file.open(QFile::WriteOnly);
        file.write(saveDoc.toJson());
        file.close();

        // WRITE TO DATABASE
        const QString assetGuid = GUIDManager::generateGUID();
        QByteArray binaryMat = QJsonDocument(materialDefOriginal).toJson();
        db->createAssetEntry(
            assetGuid,
            QFileInfo(fileName).fileName(),
            static_cast<int>(ModelTypes::Material),
            folderGuid,
            project->getProjectGuid(),
            QString(),
            QString(),
            QByteArray(),
            QByteArray(),
            QByteArray(),
            binaryMat
        );

        ThumbnailGenerator::getSingleton()->requestThumbnail(
            ThumbnailRequestType::Material, fileName, assetGuid
        );

        emit assetViewRefreshRequested();


        MaterialReader reader;
        reader.setProject(project);
        auto material = reader.parseMaterial(materialDefOriginal, db);

        // Actually create the material and add shader as it's dependency
        db->createDependency(
            static_cast<int>(ModelTypes::Material),
            static_cast<int>(ModelTypes::Shader),
            assetGuid, material->getGuid(),
            project->getProjectGuid());

        // Add all its textures as dependencies too
        auto values = materialDefOriginal["values"].toObject();
        for (const auto &prop : material->properties) {
            if (prop->type == iris::PropertyType::Texture) {
                if (!values.value(prop->name).toString().isEmpty()) {
                    db->createDependency(
                        static_cast<int>(ModelTypes::Material),
                        static_cast<int>(ModelTypes::Texture),
                        assetGuid, values.value(prop->name).toString(),
                        project->getProjectGuid()
                    );
                }
            }
        }

        // No AssetManager material payload (MATERIAL-PREVIEW-1 item c): the
        // session registry carries guids and names, never hydrated materials.
        // The one reader there ever was — the viewport's hover preview —
        // resolves through resolveMaterial now.

        // it's assumed that the thumbnail rendering will
        // be finished by the time this is executed
        QFile::remove(fileName);
    }
    else {
        qDebug() << "Need an active scenenode!";
        return;
    }
}

void SceneEditService::exportNodeTo(const iris::SceneNodePtr &node, ModelTypes modelType,
                                    const QString &filePath)
{
    if (!node) return;
    if (filePath.isEmpty() || filePath.isNull()) return;

    // Construct a temporary dir to place all the files that will be packaged
    QTemporaryDir temporaryDir;
    if (!temporaryDir.isValid()) return;

    const QString writePath = temporaryDir.path();

    // Create a blob containing the necessary tables and rows that are needed to recreate the asset
    // Assets are exported AS IS with their guids, these are changed when being reimported
    db->createBlobFromNode(node, QDir(writePath).filePath("asset.db"));

    QDir tempDir(writePath);
    tempDir.mkpath("assets");

    // The manifest contains a single string telling the asset type
    // This helps with some preliminary checks to avoid reading the db and encountering blobs etc
    QFile manifest(QDir(writePath).filePath(".manifest"));
    if (manifest.open(QIODevice::ReadWrite)) {
        QTextStream stream(&manifest);
        const int typeIndex = static_cast<int>(modelType);
        stream << (typeIndex >= 0 && typeIndex < Project::ModelTypesAsString.size()
                       ? Project::ModelTypesAsString[typeIndex]
                       : Project::ModelTypesAsString[0]);
    }
    manifest.close();

    // Collect all assets that will be exported and copy these to the temporary directory.
    //
    // TWO WALKS, ON PURPOSE (CLIPBOARD_SPEC §6.6). getChildGuids reads the
    // subtree's NODE guids AS asset guids — an identity that holds for a node
    // added straight from the library and that a PASTE or a DUPLICATE breaks by
    // design (regenerateGuids mints fresh node guids). So a pasted model used to
    // export with NO dependencies at all: a .jaf with a scene blob and an empty
    // assets/ dir, which imports as an invisible node. The key-aware closure
    // (io/assetrefs.h — the same table the clipboard uses) reads the REFERENCES
    // out of the node object instead, which is what a dependency actually is.
    // The legacy walk stays because it is still right for library-added nodes,
    // where the row itself is the asset; the union is what has to travel.
    QStringList assetGuids = AssetHelper::getChildGuids(node);
    const SceneFragment exportFragment = captureFragment(node);
    if (!exportFragment.isNull()) {
        for (const QString &guid : assetclosure::forNodes({ exportFragment.node }, db))
            if (!assetGuids.contains(guid)) assetGuids.append(guid);
    }

    for (const auto &guid : assetGuids) {
        for (const auto &assetGuid : AssetHelper::fetchAssetAndAllDependencies(guid, db)) {
            // Pin world (phase 4): bytes resolve through the project pin /
            // library source — the flat project folder holds no assets.
            QString name;
            const QString assetPath = AssetCas::resolvePinned(
                QSqlDatabase::database(), AssetStorePaths::root(),
                project->getProjectGuid(), assetGuid, &name);
            if (assetPath.isEmpty()) continue;
            if (name.isEmpty()) name = db->fetchAsset(assetGuid).name;
            if (name.isEmpty()) name = QFileInfo(assetPath).fileName();
            QFile::copy(assetPath, IrisUtils::join(writePath, "assets", name));
        }
    }

    // ONE zip loop (amendment 7): shared helper.
    ZipHelper::zipDirectory(writePath, filePath);
}
