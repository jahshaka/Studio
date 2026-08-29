#include "engineassetviewer.h"

#include <algorithm>
#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMouseEvent>
#include <QShowEvent>
#include <QStandardPaths>
#include <QWheelEvent>

#include "engineassetscene.h"
#include "enginerenderdriver.h"
#include "../engine/enginehost.h"
#include "../constants.h"
#include "../globals.h"
#include "../core/project.h"
#include "../core/database/database.h"
#include "../io/scenereader.h"
#include "../io/materialreader.hpp"
#include "../dialogs/progressdialog.h"
#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/core/property.h"
#include "irisgl/src/animation/animation.h"
#include "irisgl/src/animation/skeletalanimation.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/materials/custommaterial.h"
#include "irisgl/src/materials/defaultmaterial.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/meshnode.h"

using namespace jahshaka::engine;

static QString assetFolder(const QString &guid)
{
    return IrisUtils::join(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation),
                           Constants::ASSET_FOLDER, guid);
}

EngineAssetViewer::EngineAssetViewer(const std::shared_ptr<Engine> &engine,
                                     EngineRenderDriver *driver, QWidget *parent)
    : EngineViewWidget(parent), mEngine(engine), mDriver(driver)
{
    mScene.reset(new EngineAssetScene(engine));
    mSource = new iris::SceneSource();
    mProgress = new ProgressDialog();
    mProgress->setWindowModality(Qt::WindowModal);
    mProgress->setRange(0, 100);
    setMouseTracking(true);              // AssetViewer: needed for mouse events
    setFocusPolicy(Qt::ClickFocus);      // AssetViewer: needed for key events
    mFrameTimer.start();
    if (mDriver)
        connect(mDriver, &EngineRenderDriver::beforeFrame, this, &EngineAssetViewer::syncFrame);
}

EngineAssetViewer::~EngineAssetViewer()
{
    mActive = false;
    mScene->release();      // the Scene goes before the View (Engine.h ordering)
    mScene.reset();
    destroyView();
    delete mSource;
    delete mProgress;
}

void EngineAssetViewer::showEvent(QShowEvent *e)
{
    EngineViewWidget::showEvent(e);
    if (!view() && mEngine)
        createView(mEngine, "assets-view-" + QString::number(reinterpret_cast<uintptr_t>(this)),
                   Colour(25 / 255.0f, 25 / 255.0f, 25 / 255.0f));
    if (view()) mScene->attach(view());
    mActive = true;
    mFrameTimer.restart();
}

void EngineAssetViewer::hideEvent(QHideEvent *e)
{
    EngineViewWidget::hideEvent(e);
    mActive = false;
}

void EngineAssetViewer::syncFrame()
{
    if (!mActive || !view() || !isVisible()) return;
    if (!mScene->attach(view())) return;
    const float dt = std::max(0.001f, float(mFrameTimer.restart()) / 1000.0f);
    mScene->step(dt, width(), height());
}

// ---- mouse: AssetViewer's convention (deltas negated) ----

void EngineAssetViewer::mousePressEvent(QMouseEvent *e)
{
    EngineViewWidget::mousePressEvent(e);
    mPrevMousePos = e->position();
    mScene->mouseDown(e->button());
}

void EngineAssetViewer::mouseMoveEvent(QMouseEvent *e)
{
    EngineViewWidget::mouseMoveEvent(e);
    const QPointF pos = e->position();
    const QPointF dir = pos - mPrevMousePos;
    mScene->mouseMove(int(-dir.x()), int(-dir.y()));
    mPrevMousePos = pos;
}

void EngineAssetViewer::mouseReleaseEvent(QMouseEvent *e)
{
    EngineViewWidget::mouseReleaseEvent(e);
    mScene->mouseUp(e->button());
}

void EngineAssetViewer::wheelEvent(QWheelEvent *e)
{
    mScene->wheel(e->angleDelta().y());
}

// ---- IAssetViewer ----

void EngineAssetViewer::clearScene()
{
    mScene->clearSubject();
}

void EngineAssetViewer::changeBackdrop(unsigned int id)
{
    mScene->setBackdrop(id);
}

void EngineAssetViewer::addNodeToScene(iris::SceneNodePtr sceneNode, QString guid, bool viewed, bool cache, bool isOnGround)
{
    if (!sceneNode) return;
    mirrorableMaterials(sceneNode);
    mScene->setSubject(sceneNode, viewed, isOnGround);
    if (cache) mCachedAssets.insert(guid, sceneNode);
}

void EngineAssetViewer::cacheCurrentModel(QString guid)
{
    // AssetViewer::cacheCurrentModel: everything that is not a light.
    auto doc = mScene->document();
    if (doc->rootNode->hasChildren()) {
        for (auto child : doc->rootNode->children) {
            if (child->sceneNodeType != iris::SceneNodeType::Light && !child->isBuiltIn)
                mCachedAssets.insert(guid, child);
        }
    }
}

void EngineAssetViewer::orientCamera(QVector3D pos, QVector3D localRot, int distanceFromPivot)
{
    mScene->orientCamera(pos, localRot, float(distanceFromPivot));
}

QJsonObject EngineAssetViewer::getSceneProperties()
{
    return mScene->sceneProperties();
}

void EngineAssetViewer::showProgress()
{
    mProgress->setLabelText(tr("Loading asset preview..."));
    mProgress->show();
    QApplication::processEvents();
}

void EngineAssetViewer::hideProgress()
{
    mProgress->close();
}

void EngineAssetViewer::loadJafModel(QString path, QString guid, bool firstAdd, bool cache, bool firstLoad)
{
    Q_UNUSED(firstAdd);
    showProgress();
    auto node = readJafModel(path, guid);
    if (node) {
        addNodeToScene(node, QFileInfo(path).baseName(), false, true);   // legacy: always cached
        if (firstLoad) mScene->resetCamera();
        else mScene->resetCameraAfter();
    }
    hideProgress();
}

void EngineAssetViewer::loadJafMaterial(QString guid, bool firstAdd, bool cache, bool firstLoad)
{
    Q_UNUSED(firstAdd); Q_UNUSED(cache);
    showProgress();
    mScene->setSkyColor(QColor(25, 25, 25));
    auto node = mScene->setMaterialSubject(mirrorable(readJafMaterial(guid)), "ae98cx7u_mat_ball");
    mCachedAssets.insert(guid, node);
    if (!firstLoad) mScene->resetCameraAfter();
    hideProgress();
}

void EngineAssetViewer::loadJafShader(QString guid, QMap<QString, QString> &outGuids, bool firstAdd, bool cache, bool firstLoad)
{
    Q_UNUSED(outGuids); Q_UNUSED(firstAdd); Q_UNUSED(cache);
    showProgress();
    mScene->setSkyColor(QColor(25, 25, 25));
    auto node = mScene->setMaterialSubject(mirrorable(readJafShader(guid)), "ae98cx7u_shader_ball");
    mCachedAssets.insert(guid, node);
    if (!firstLoad) mScene->resetCameraAfter();
    hideProgress();
}

void EngineAssetViewer::loadJafSky(QString guid, bool firstAdd, bool cache, bool firstLoad)
{
    Q_UNUSED(firstAdd); Q_UNUSED(cache);
    showProgress();
    applyJafSky(guid);
    if (firstLoad) mScene->resetCamera();
    else mScene->resetCameraAfter();
    hideProgress();
}

void EngineAssetViewer::loadModel(QString path, QString guid, bool firstAdd, bool cache, bool firstLoad)
{
    // AssetViewer::loadModel: the library entry already exists by now.
    loadJafModel(path, guid, firstAdd, cache, firstLoad);
}

QImage EngineAssetViewer::takeScreenshot(int width, int height)
{
    return mScene->renderImage(width, height);
}

// ---- database -> document (AssetViewer::addJaf*) ----

iris::SceneNodePtr EngineAssetViewer::readJafModel(const QString &path, const QString &guid)
{
    if (!mDb) return iris::SceneNodePtr();
    QJsonObject objectHierarchy = QJsonDocument::fromJson(mDb->fetchAssetData(guid)).object();

    SceneReader reader;
    reader.setBaseDirectory(assetFolder(guid));
    iris::SceneNodePtr node = reader.readSceneNode(objectHierarchy);
    if (!node) return node;

    // rename animation sources to relative paths
    if (Globals::project) {
        auto relativePath = QDir(Globals::project->folderPath).relativeFilePath(path);
        for (auto anim : node->getAnimations()) {
            if (!!anim->skeletalAnimation) anim->skeletalAnimation->source = relativePath;
        }
    }
    return node;
}

iris::MaterialPtr EngineAssetViewer::readJafMaterial(const QString &guid)
{
    if (!mDb) return iris::MaterialPtr();
    QJsonObject matObject = QJsonDocument::fromJson(mDb->fetchAssetData(guid)).object();
    MaterialReader reader(TextureSource::GlobalAssets, assetFolder(guid));
    return reader.parseMaterial(matObject, mDb);
}

iris::MaterialPtr EngineAssetViewer::readJafShader(const QString &guid)
{
    if (!mDb) return iris::MaterialPtr();
    const QString assetPath = assetFolder(guid);
    auto shaderDefinition = QJsonDocument::fromJson(mDb->fetchAssetData(guid)).object();
    auto vAsset = mDb->fetchAsset(shaderDefinition["vertex_shader"].toString());
    auto fAsset = mDb->fetchAsset(shaderDefinition["fragment_shader"].toString());
    if (!vAsset.name.isEmpty()) shaderDefinition["vertex_shader"] = QDir(assetPath).filePath(vAsset.name);
    if (!fAsset.name.isEmpty()) shaderDefinition["fragment_shader"] = QDir(assetPath).filePath(fAsset.name);

    // The GLSL itself cannot run on the engine (it is AZSL/Hlms territory); the
    // shader's colour and texture uniforms still drive the preview material.
    iris::CustomMaterialPtr material = iris::CustomMaterialPtr::create();
    material->generate(shaderDefinition);
    return material;
}

void EngineAssetViewer::applyJafSky(const QString &guid)
{
    if (!mDb) return;
    auto scene = mScene->document();
    scene->skyGuid = guid;
    QJsonObject skyProperties = QJsonDocument::fromJson(mDb->fetchAsset(guid).properties).object().value("sky").toObject();
    QJsonObject skyData = QJsonDocument::fromJson(mDb->fetchAssetData(guid)).object();
    scene->skyType = static_cast<iris::SkyType>(skyProperties.value("type").toInt());
    const QString assetPath = assetFolder(guid);

    if (scene->skyType == iris::SkyType::SINGLE_COLOR) {
        scene->skyColor = SceneReader::readColor(skyData.value("skyColor").toObject());
    }
    else if (scene->skyType == iris::SkyType::EQUIRECTANGULAR) {
        QStringList dependency = mDb->fetchAssetDependeesByType(guid, ModelTypes::Texture);
        if (!dependency.isEmpty()) {
            auto image = IrisUtils::join(assetPath, mDb->fetchAsset(dependency.first()).name);
            if (QFileInfo(image).isFile()) scene->setSkyTexture(iris::Texture2D::load(image, false));
        }
    }
    else if (scene->skyType == iris::SkyType::GRADIENT) {
        scene->gradientTop = SceneReader::readColor(skyData.value("gradientTop").toObject());
        scene->gradientMid = SceneReader::readColor(skyData.value("gradientMid").toObject());
        scene->gradientBot = SceneReader::readColor(skyData.value("gradientBot").toObject());
        scene->gradientOffset = skyData.value("gradientOffset").toDouble();
    }
    // Realistic, cubemap and material skies are not on the engine yet
    // (MORNING_CHECKLIST: known gaps); the view keeps its background colour.
}

// ---- materials the mirror can render ----

iris::MaterialPtr EngineAssetViewer::mirrorable(iris::MaterialPtr material)
{
    if (!material) return iris::DefaultMaterial::create().staticCast<iris::Material>();
    auto custom = material.dynamicCast<iris::CustomMaterial>();
    if (!custom) return material;

    auto out = iris::DefaultMaterial::create();
    bool any = false;
    for (auto prop : custom->properties) {
        if (!prop) continue;
        const QVariant v = prop->getValue();
        if (prop->type == iris::PropertyType::Color) {
            const QColor c = v.value<QColor>();
            if (prop->name == "diffuseColor")       { out->setDiffuseColor(c);  any = true; }
            else if (prop->name == "specularColor") { out->setSpecularColor(c); }
            else if (prop->name == "ambientColor")  { out->setAmbientColor(c); }
        }
        else if (prop->type == iris::PropertyType::Texture) {
            const QString path = v.toString();
            if (path.isEmpty() || !QFileInfo(path).isFile()) continue;
            if (prop->name == "diffuseTexture")       { out->setDiffuseTexture(iris::Texture2D::load(path));  any = true; }
            else if (prop->name == "normalTexture")   { out->setNormalTexture(iris::Texture2D::load(path)); }
            else if (prop->name == "specularTexture") { out->setSpecularTexture(iris::Texture2D::load(path)); }
        }
        else if (prop->type == iris::PropertyType::Float) {
            if (prop->name == "shininess")            out->setShininess(v.toFloat());
            else if (prop->name == "textureScale")    out->setTextureScale(v.toFloat());
            else if (prop->name == "normalIntensity") out->setNormalIntensity(v.toFloat());
        }
    }
    Q_UNUSED(any);
    return out.staticCast<iris::Material>();
}

void EngineAssetViewer::mirrorableMaterials(iris::SceneNodePtr node)
{
    if (!node) return;
    if (node->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = node.staticCast<iris::MeshNode>();
        auto mat = meshNode->getMaterial();
        if (mat && mat.dynamicCast<iris::CustomMaterial>()) meshNode->setMaterial(mirrorable(mat));
    }
    for (auto child : node->children) mirrorableMaterials(child);
}

IAssetViewer *createEngineAssetViewer(const std::shared_ptr<Engine> &engine,
                                      EngineRenderDriver *driver, QWidget *parent)
{
    return new EngineAssetViewer(engine, driver, parent);
}
