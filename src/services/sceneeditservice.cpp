/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

For more information see the LICENSE file
*************************************************************************/

#include "services/sceneeditservice.h"

#include "services/assetcas.h"
#include "services/assetstorepaths.h"

#include <QBuffer>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMatrix3x3>
#include <QPixmap>
#include <QQuaternion>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QTextStream>

#include "irisgl/core/irisutils.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/materials/pbrmaterial.h"
#include "irisgl/document/physics/environment.h"
#include "irisgl/document/scenegraph/decalnode.h"
#include "irisgl/document/scenegraph/lightnode.h"
#include "irisgl/document/scenegraph/meshnode.h"
#include "irisgl/document/scenegraph/particlesystemnode.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/viewernode.h"
#include "irisgl/document/scenegraph/cameranode.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/keyframeset.h"
#include "irisgl/document/animation/keyframeanimation.h"

#include "commands/addscenenodecommand.h"
#include "commands/changematerialcommand.h"
#include "commands/deletescenenodecommand.h"
#include "data/constants.h"
#include "services/assethelper.h"
#include "data/database/database.h"
#include "data/guidmanager.h"
#include "data/materialpreset.h"
#include "services/assetmetadata.h"
#include "services/imagematerial.h"
#include "services/projectassets.h"
#include "services/scenenodehelper.h"
#include "services/thumbnailmanager.h"
#include "viewport/ieditorviewport.h"
#include "services/thumbnailgenerator.h"
#include "bridge/enginehost.h"
#include "io/assetmanager.h"
#include "io/ziphelper.h"
#include "io/materialreader.h"
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

void SceneEditService::addBuiltinPrimitive(const QString &meshPath, const QString &name)
{
    const QString nodeGuid = GUIDManager::generateGUID();
    iris::MeshNodePtr node = SceneNodeHelper::createBasicMeshNode(meshPath, name, nodeGuid);
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
    addNodeToScene(node);
}

void SceneEditService::addPlane()    { addBuiltinPrimitive(":/content/primitives/plane.obj", "Plane"); }
void SceneEditService::addGround()   { addBuiltinPrimitive(":/models/ground.obj", "Ground"); }
void SceneEditService::addCone()     { addBuiltinPrimitive(":/content/primitives/cone.obj", "Cone"); }
// "Plane" is not a typo: the pre-extraction addCapsule() named its node Plane
// and the extraction preserves behaviour bit-for-bit.
void SceneEditService::addCapsule()  { addBuiltinPrimitive(":/content/primitives/capsule.obj", "Plane"); }
void SceneEditService::addCube()     { addBuiltinPrimitive(":/content/primitives/cube.obj", "Cube"); }
void SceneEditService::addTorus()    { addBuiltinPrimitive(":/content/primitives/torus.obj", "Torus"); }
void SceneEditService::addSphere()   { addBuiltinPrimitive(":/content/primitives/sphere.obj", "Sphere"); }
void SceneEditService::addCylinder() { addBuiltinPrimitive(":/content/primitives/cylinder.obj", "Cylinder"); }
void SceneEditService::addPyramid()  { addBuiltinPrimitive(":/content/primitives/pyramid.obj", "Pyramid"); }
void SceneEditService::addTeapot()   { addBuiltinPrimitive(":/content/primitives/teapot.obj", "Teapot"); }
void SceneEditService::addSponge()   { addBuiltinPrimitive(":/content/primitives/sponge.obj", "Sponge"); }
void SceneEditService::addSteps()    { addBuiltinPrimitive(":/content/primitives/steps.obj", "Steps"); }
void SceneEditService::addGear()     { addBuiltinPrimitive(":/content/primitives/gear.obj", "Gear"); }

void SceneEditService::addPrimitive(const QString &text)
{
    if (text == "Plane")    addPlane();
    if (text == "Cone")     addCone();
    if (text == "Cube")     addCube();
    if (text == "Cylinder") addCylinder();
    if (text == "Sphere")   addSphere();
    if (text == "Torus")    addTorus();
    if (text == "Capsule")  addCapsule();
    if (text == "Gear")     addGear();
    if (text == "Pyramid")  addPyramid();
    if (text == "Teapot")   addTeapot();
    if (text == "Sponge")   addSponge();
    if (text == "Steps")    addSteps();
}

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
    node->shadowMap->shadowType = iris::ShadowMapType::Soft;
    node->setLightType(iris::LightType::Directional);
    node->icon = iris::Texture2D::load(":/icons/light.png");   // the sun glyph
    node->setName("Directional Light");
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

void SceneEditService::addEmpty()
{
    auto node = iris::SceneNode::create();
    node->setName("Empty");
    addNodeToScene(node);
}

void SceneEditService::addViewer()
{
    auto node = iris::ViewerNode::create();
    node->setName("Avatar");
    addNodeToScene(node);

    auto scene = this->scene();

    // Set all other controllers to false
    for (auto child : scene->getRootNode()->children) {
        if (child->getSceneNodeType() == iris::SceneNodeType::Viewer) {
            child.staticCast<iris::ViewerNode>()->setActiveCharacterController(false);
        }
    }

    node->setActiveCharacterController(true);
    scene->getPhysicsEnvironment()->addCharacterControllerToWorldUsingNode(node);
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

    auto fguid = GUIDManager::generateGUID();
    if (!db->checkIfRecordExists("name", "Systems", "folders", false, project->getProjectGuid())) {
        if (!db->createFolder("Systems", project->getProjectGuid(), fguid, project->getProjectGuid(), false))
            return iris::ParticleSystemNodePtr();
    }

    auto nodeGuid = GUIDManager::generateGUID();
    node->setGUID(nodeGuid);
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

    // if we reached this far, the project dir has already been created
    // we can copy some default assets to each project here
    QFile::copy(IrisUtils::getAbsoluteAssetPath("app/images/default_particle.jpg"),
        QDir(project->getProjectFolder()).filePath("Glowing Particle.jpg"));

    auto thumb = ThumbnailManager::createThumbnail(
        IrisUtils::getAbsoluteAssetPath("app/images/default_particle.jpg"), 72, 72);

    QByteArray thumbnailBytes;
    QBuffer buffer(&thumbnailBytes);
    buffer.open(QIODevice::WriteOnly);
    QPixmap::fromImage(*thumb->thumb).save(&buffer, "PNG");

    const QString tileGuid = GUIDManager::generateGUID();
    const QString assetGuid = db->createAssetEntry(tileGuid,
        "Glowing Particle.jpg",
        static_cast<int>(ModelTypes::Texture),
        project->getProjectGuid(),
        project->getProjectGuid(),
        QString(),
        QString(),
        thumbnailBytes);

    db->createDependency(
        static_cast<int>(ModelTypes::ParticleSystem),
        static_cast<int>(ModelTypes::Texture),
        nodeGuid, assetGuid,
        project->getProjectGuid()
    );

    {
        QString texPath = QDir(project->getProjectFolder()).filePath("Glowing Particle.jpg");
        node->setTexture(iris::Texture2D::load(texPath));
    }

    auto assetTexture = new AssetTexture;
    assetTexture->fileName = "Glowing Particle.jpg";
    assetTexture->assetGuid = assetGuid;
    assetTexture->path = QDir(project->getProjectFolder()).filePath("Glowing Particle.jpg");
    AssetManager::addAsset(assetTexture);

    addNodeToScene(node);
    return node;
}

void SceneEditService::addMesh(const QString &path, bool ignore, QVector3D position)
{
    if (path.isEmpty()) return;

    // No SceneSource: the loader owns a local importer when none is passed
    // (the old `new` here leaked the whole parsed scene per added mesh).
    auto node = iris::MeshNode::loadAsSceneFragment(path, [](iris::MeshPtr mesh, iris::MeshMaterialData& data)
    {
        auto mat = iris::CustomMaterial::create();
        mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));

        mat->setValue("diffuseColor", data.diffuseColor);
        mat->setValue("specularColor", data.specularColor);
        mat->setValue("ambientColor", data.ambientColor);
        mat->setValue("emissionColor", data.emissionColor);

        mat->setValue("shininess", data.shininess);

        if (QFile(data.diffuseTexture).exists() && QFileInfo(data.diffuseTexture).isFile())
            mat->setValue("diffuseTexture", data.diffuseTexture);

        if (QFile(data.specularTexture).exists() && QFileInfo(data.specularTexture).isFile())
            mat->setValue("specularTexture", data.specularTexture);

        if (QFile(data.normalTexture).exists() && QFileInfo(data.normalTexture).isFile())
            mat->setValue("normalTexture", data.normalTexture);

        return mat;
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

void SceneEditService::addMaterialMesh(const QString &path, bool ignore, QVector3D position,
                                       const QString &guid, const QString &assetName)
{
    Q_UNUSED(path);
    Q_UNUSED(assetName);
    auto document = QJsonDocument::fromJson(db->fetchAssetData(guid)).object();

    auto reader = new SceneReader;
    reader->setDatabaseHandle(db);
    reader->setProject(project);
    reader->setBaseDirectory(project->getProjectFolder());
    iris::SceneNodePtr node = reader->readSceneNode(document);
    delete reader;

    // rename animation sources to relative paths
    QString meshGuid = db->fetchObjectMesh(guid, static_cast<int>(ModelTypes::Object), static_cast<int>(ModelTypes::Mesh));
    auto relPath = QDir(project->folderPath).relativeFilePath(db->fetchAsset(meshGuid).name);
    for (auto anim : node->getAnimations()) if (!!anim->skeletalAnimation) anim->skeletalAnimation->source = relPath;

    // Honour the drop position (the viewport computed where the cursor hit the
    // scene) — legacy addMesh does the same; without this every dropped asset
    // landed at the asset's authored origin (ASSET_ADD_AUDIT D1).
    node->setLocalPos(position);

    addNodeToScene(node, ignore);
}

iris::MeshNodePtr SceneEditService::addImagePlane(const QString &textureGuid,
                                                  QVector3D position,
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
        nodeGuid);

    // plane.obj is a 2x2 XZ quad — 0.5 * the aspect-normalized extents caps
    // the long side at exactly 1 m.
    const float wf = static_cast<float>(w), hf = static_cast<float>(h);
    node->setLocalScale(0.5f * QVector3D(w >= h ? 1.0f : wf / hf,
                                         1.0f,
                                         h > w ? 1.0f : hf / wf));

    // Billboard-once (§5, owner call §8.4): rotate the +Y plane normal onto
    // the direction to the editor camera, roll aligned to the camera's up.
    // Afterwards the node is completely ordinary.
    if (viewport && viewport->editorCamera()) {
        auto cam = viewport->editorCamera();
        const QVector3D toCam = (cam->getGlobalPosition() - position).normalized();
        if (!toCam.isNull()) {
            QVector3D upHint = cam->getGlobalRotation().rotatedVector(QVector3D(0, 1, 0));
            QVector3D right = QVector3D::crossProduct(upHint, toCam);
            if (right.lengthSquared() < 1e-6f)  // camera straight above/below
                right = QVector3D::crossProduct(QVector3D(0, 0, -1), toCam);
            right.normalize();
            // Local +X → right, +Y (the normal) → toCam, +Z → right × toCam
            // (the plane's V axis runs along +Z, so the image top faces the
            // camera's up).
            const QVector3D zAxis = QVector3D::crossProduct(right, toCam);
            const float m[9] = { right.x(), toCam.x(), zAxis.x(),
                                 right.y(), toCam.y(), zAxis.y(),
                                 right.z(), toCam.z(), zAxis.z() };
            node->setLocalRot(QQuaternion::fromRotationMatrix(QMatrix3x3(m)));
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
    if (!decal) return false;
    const QString oldGuid = decal->textureGuid;
    decal->textureGuid = textureGuid;
    decal->resolvedTexturePath.clear();
    if (textureGuid.isEmpty()) {
        if (db && project && !oldGuid.isEmpty() && !project->getProjectGuid().isEmpty())
            db->deleteDependency(decal->getGUID(), oldGuid);
        return true;
    }
    if (!db || !project || project->getProjectGuid().isEmpty()) return false;

    // BINDING membership, never a direct add: a decal REFERS to an existing
    // image, so no companion PBR material is minted for it (the shared rule the
    // lights lane landed — LIGHTS_COMPLETION_SPEC D4 / ProjectAssets::AddKind).
    ProjectAssets::addToProject(textureGuid, db, project, ProjectAssets::AddKind::Binding);
    if (!oldGuid.isEmpty() && oldGuid != textureGuid)
        db->deleteDependency(decal->getGUID(), oldGuid);
    db->createDependency(static_cast<int>(ModelTypes::Object),
                         static_cast<int>(ModelTypes::Texture),
                         decal->getGUID(), textureGuid, project->getProjectGuid());

    decal->resolvedTexturePath = AssetCas::resolvePinned(
        QSqlDatabase::database(), AssetStorePaths::root(),
        project->getProjectGuid(), textureGuid);
    return true;
}

void SceneEditService::addAssetParticleSystem(bool ignore, QVector3D position, QString guid,
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
        // pinned bytes in project context) — the flat projectFolder/name join
        // pointed at a folder the pipeline no longer populates.
        auto textureGuid = pDefs["texture"].toString();
        if (!textureGuid.isEmpty()) {
            QSqlDatabase conn = QSqlDatabase::database();
            QString texPath = AssetCas::resolvePinned(conn, AssetStorePaths::root(),
                                                      project->getProjectGuid(), textureGuid);
            if (texPath.isEmpty())
                texPath = IrisUtils::join(project->getProjectFolder(),
                                          db->fetchAsset(textureGuid).name);
            particleNode->setTexture(iris::Texture2D::load(texPath));
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

    emit hierarchyChanged();
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
        auto offset = viewport->editorCamera()->getLocalRot().rotatedVector(QVector3D(0, -1.0f, -spawnDist));
        offset += viewport->editorCamera()->getLocalPos();
        sceneNode->setLocalPos(offset);
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

    if (node->sceneNodeType == iris::SceneNodeType::Viewer) {
        scene()->getPhysicsEnvironment()->removeCharacterControllerFromWorld(node->getGUID());
    }

    // The command owns the asset-row cleanup: the row is deleted only when the
    // delete becomes permanent, so undo no longer resurrects a node whose DB
    // asset is gone (SCRIPTING_SPEC §1.2).
    auto cmd = new DeleteSceneNodeCommand(node->parent, node,
                                          node->isBuiltIn ? db : nullptr, node->getGUID());
    undo->push(cmd);
    return true;
}

iris::SceneNodePtr SceneEditService::duplicateNode(iris::SceneNodePtr source)
{
    if (!scene()) return iris::SceneNodePtr();
    if (!source || !source->isDuplicable()) return iris::SceneNodePtr();

    auto node = source->duplicate();
    // Undoable now (SCRIPTING_SPEC §1.2): the add command parents the copy,
    // refreshes the hierarchy and selects it — the manual addChild+repopulate
    // this slot used to do, minus the missing undo entry.
    undo->push(new AddSceneNodeCommand(source->parent, node));
    return node;
}

namespace {

// Every mesh node at or under `node`. An imported model roots at an Empty
// container node, and the viewport's click-selects-the-root rule hands exactly
// that container to the material paths — a mesh-only guard silently dropped
// the apply on the floor (the "PBR materials lost on reopen" data loss: the
// materials never entered the document, so the writer had nothing to save).
void collectMeshNodes(const iris::SceneNodePtr &node, QList<iris::MeshNodePtr> &out)
{
    if (!node) return;
    if (node->sceneNodeType == iris::SceneNodeType::Mesh)
        out.append(node.staticCast<iris::MeshNode>());
    for (const auto &child : node->children) collectMeshNodes(child, out);
}

// Builds a fresh material instance from a preset. A PBR preset builds a
// PbrMaterial; anything else takes the legacy CustomMaterial path, so existing
// presets behave exactly as before.
iris::MaterialPtr materialFromPreset(const MaterialPreset &preset)
{
    iris::MaterialPtr mat;

    if (preset.type.compare("PBR", Qt::CaseInsensitive) == 0) {
        auto pbr = iris::PbrMaterial::create();

        pbr->setValue("baseColor",           preset.baseColor);
        pbr->setValue("metallic",            preset.metallic);
        pbr->setValue("roughness",           preset.roughness);
        pbr->setValue("roughnessLowerBound", preset.roughnessLowerBound);
        pbr->setValue("roughnessUpperBound", preset.roughnessUpperBound);
        pbr->setValue("normalFactor",        preset.pbrNormalFactor);
        pbr->setValue("occlusionFactor",     preset.occlusionFactor);
        pbr->setValue("emissiveColor",       preset.emissiveColor);
        pbr->setValue("emissiveIntensity",   preset.emissiveIntensity);
        pbr->setValue("textureScale",        preset.textureScale);
        pbr->setValue("alphaMode",           preset.alphaMode);
        pbr->setValue("alpha",               preset.alpha);
        pbr->setValue("alphaCutoff",         preset.alphaCutoff);

        pbr->setValue("baseColorMap",  preset.baseColorMap);
        pbr->setValue("metallicMap",   preset.metallicMap);
        pbr->setValue("roughnessMap",  preset.roughnessMap);
        pbr->setValue("normalMap",     preset.pbrNormalMap);
        pbr->setValue("occlusionMap",  preset.occlusionMap);
        pbr->setValue("emissiveMap",   preset.emissiveMap);

        mat = pbr;
    }
    else {

    auto m = iris::CustomMaterial::create();
    m->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));

    m->setValue("diffuseTexture", preset.diffuseTexture);
    m->setValue("specularTexture", preset.specularTexture);
    m->setValue("normalTexture", preset.normalTexture);
    m->setValue("reflectionTexture", preset.reflectionTexture);

    m->setValue("ambientColor", preset.ambientColor);
    m->setValue("diffuseColor", preset.diffuseColor);
    m->setValue("specularColor", preset.specularColor);

    m->setValue("shininess", preset.shininess);
    m->setValue("normalIntensity", preset.normalIntensity);
    m->setValue("reflectionInfluence", preset.reflectionInfluence);
    m->setValue("textureScale", preset.textureScale);

    mat = m;
    }

    return mat;
}

} // namespace

void SceneEditService::applyMaterialPreset(const MaterialPreset &preset)
{
    applyMaterialPreset(preset, selection->selected());
}

void SceneEditService::applyMaterialPreset(const MaterialPreset &preset, iris::SceneNodePtr target)
{
    QList<iris::MeshNodePtr> meshes;
    collectMeshNodes(target, meshes);
    if (meshes.isEmpty()) return;

    // Each mesh gets its OWN instance: sharing one material across nodes makes
    // a later per-node edit bleed across the whole model. One undo entry
    // reverses the whole apply (a preset landing on a model root repaints
    // every descendant mesh — that has to be reversible).
    iris::MaterialPtr mat;
    undo->stack()->beginMacro(QObject::tr("Apply Material Preset"));
    for (const auto &meshNode : meshes) {
        mat = materialFromPreset(preset);
        undo->push(new ChangeMaterialCommand(meshNode, mat));
    }
    undo->stack()->endMacro();

    // The registration tail below (texture copies, matgen.material, asset
    // entry, thumbnail) is per-APPLY, not per-mesh; only the Object->Material
    // dependency rows are per-mesh.

    auto fguid = GUIDManager::generateGUID();
    if (!db->checkIfRecordExists("name", "Presets", "folders", false, project->getProjectGuid())) {
        if (!db->createFolder("Presets", project->getProjectGuid(), fguid, project->getProjectGuid(), false)) return;
    }

    // Copy the preset's textures into the project and register them FIRST:
    // writeSceneNodeMaterial stores textures as asset guids resolved by file
    // name, so serializing before these rows existed left the saved material
    // asset with dangling texture references (the reapplied texture vanished).
    QStringList textureGuids;
    for (const auto &prop : mat->properties) {
        if (prop->type == iris::PropertyType::Texture) {
            auto file = prop->getValue().toString();
            if (file.isEmpty()) continue;
            const QString fileName = QFileInfo(file).fileName();

            // One row per file: reapplying a preset must not duplicate it.
            QString fileGuid = db->fetchAssetGUIDByName(fileName, project->getProjectGuid());
            if (fileGuid.isEmpty()) {
                fileGuid = db->createAssetEntry(
                    GUIDManager::generateGUID(),
                    fileName,
                    static_cast<int>(ModelTypes::Texture),
                    fguid,
                    project->getProjectGuid(),
                    QString(),
                    QString(),
                    QByteArray(),
                    QByteArray(),
                    QByteArray()
                );
            }
            // Pin world (phase 4): the texture's bytes go into the CAS under
            // the row's guid and the project pins them — no flat project-
            // folder copy, and the readers resolve pin-first.
            {
                QSqlDatabase conn = QSqlDatabase::database();
                const QString root = AssetStorePaths::root();
                QString oid, casError;
                if (AssetCas::ingestFile(conn, root, file, fileGuid,
                                         QStringLiteral("source"), fileName, &oid, &casError))
                    AssetCas::writePin(conn, project->getProjectGuid(), fileGuid, oid);
            }
            textureGuids.append(fileGuid);
        }
    }

    QJsonObject material;
    SceneWriter::writeSceneNodeMaterial(material, mat);

    QFile jsonFile(QDir(project->getProjectFolder()).filePath("matgen.material"));
    jsonFile.open(QFile::WriteOnly);
    jsonFile.write(QJsonDocument(material).toJson());

    QString guid = db->createAssetEntry(
        GUIDManager::generateGUID(),
        preset.name,
        static_cast<int>(ModelTypes::Material),
        fguid,
        project->getProjectGuid(),
        QString(),      // license
        QString(),      // author
        QByteArray(),   // thumbnail
        QByteArray(),   // properties
        QByteArray(),   // tags
        // The material definition goes into the ASSET column. One missing
        // argument used to shift it into `tags`, leaving every registered
        // preset material an empty shell — un-appliable and hydrating broken.
        QJsonDocument(material).toJson()
    );

    for (const auto &textureGuid : textureGuids) {
        db->createDependency(
            static_cast<int>(ModelTypes::Material),
            static_cast<int>(ModelTypes::Texture),
            guid,
            textureGuid,
            project->getProjectGuid()
        );
    }

    ThumbnailGenerator::getSingleton()->requestThumbnail(
        ThumbnailRequestType::Material, QDir(project->getProjectFolder()).filePath("matgen.material"), guid
    );

    emit assetViewRefreshRequested();

    for (const auto &meshNode : meshes) {
        db->createDependency(
            static_cast<int>(ModelTypes::Object),
            static_cast<int>(ModelTypes::Material),
            meshNode->getGUID(),
            guid,
            project->getProjectGuid()
        );
    }

    // TODO: update node's material without updating the whole ui
    emit materialApplied(preset.type);
}

bool SceneEditService::applyMaterialAsset(const QString &assetGuid, iris::SceneNodePtr target)
{
    QList<iris::MeshNodePtr> meshes;
    collectMeshNodes(target, meshes);
    if (meshes.isEmpty()) return false;

    const QJsonObject matObject =
        QJsonDocument::fromJson(db->fetchAssetData(assetGuid)).object();
    if (matObject.isEmpty()) return false;

    MaterialReader reader;
    reader.setProject(project);

    undo->stack()->beginMacro(QObject::tr("Apply Material"));
    for (const auto &meshNode : meshes) {
        // Fresh instance per mesh; parseMaterialTyped dispatches on the stored
        // materialType, so a saved PBR material comes back as a real
        // PbrMaterial instead of a broken shader-less CustomMaterial.
        auto mat = reader.parseMaterialTyped(matObject, db);
        if (!mat) continue;
        undo->push(new ChangeMaterialCommand(meshNode, mat));
    }
    undo->stack()->endMacro();

    for (const auto &meshNode : meshes) {
        db->createDependency(
            static_cast<int>(ModelTypes::Object),
            static_cast<int>(ModelTypes::Material),
            meshNode->getGUID(),
            assetGuid,
            project->getProjectGuid()
        );
    }

    emit materialApplied(matObject["materialType"].toString() == "pbr"
                             ? QStringLiteral("PBR")
                             : QStringLiteral("custom"));
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
            node.staticCast<iris::MeshNode>()->getMaterial().staticCast<iris::CustomMaterial>()
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

        auto assetMat = new AssetMaterial;
        assetMat->assetGuid = assetGuid;
        assetMat->setValue(QVariant::fromValue(material));
        AssetManager::addAsset(assetMat);

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
        stream << Project::ModelTypesAsString[static_cast<int>(modelType)];
    }
    manifest.close();

    // Collect all assets that will be exported and copy these to the temporary directory
    QStringList assetGuids = AssetHelper::getChildGuids(node);

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
