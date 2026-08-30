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
#include "../editor/enginethumbnailrenderer.h"
#include "../engine/enginehost.h"
#include "../constants.h"
#include "../globals.h"
#include "../core/project.h"
#include "../core/database/database.h"
#include "../io/scenereader.h"
#include "../io/materialreader.hpp"
#include "../dialogs/progressdialog.h"
#include "irisgl/core/irisutils.h"
#include "irisgl/core/properties/property.h"
#include "irisgl/document/animation/animation.h"
#include "irisgl/document/animation/skeletalanimation.h"
#include "irisgl/document/assets/texture2d.h"
#include "irisgl/document/materials/custommaterial.h"
#include "irisgl/document/materials/defaultmaterial.h"
#include "irisgl/document/scenegraph/scene.h"
#include "irisgl/document/scenegraph/scenenode.h"
#include "irisgl/document/scenegraph/meshnode.h"

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
    e->accept();   // never let the press propagate to a container filter
    mPrevMousePos = e->position();
    mScene->mouseDown(e->button());
}

void EngineAssetViewer::mouseMoveEvent(QMouseEvent *e)
{
    e->accept();   // never let the press propagate to a container filter
    const QPointF pos = e->position();
    const QPointF dir = pos - mPrevMousePos;
    mScene->mouseMove(int(-dir.x()), int(-dir.y()));
    mPrevMousePos = pos;
}

void EngineAssetViewer::mouseReleaseEvent(QMouseEvent *e)
{
    e->accept();   // never let the press propagate to a container filter
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
    reader.setDatabaseHandle(mDb);   // resolves mesh and texture GUIDs to store files
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
    // One conversion for previews AND thumbnails (they diverged once: thumbnails
    // dropped the textures and rendered grey).
    return EngineThumbnailRenderer::previewMaterialFor(material);
}

void EngineAssetViewer::mirrorableMaterials(iris::SceneNodePtr node)
{
    EngineThumbnailRenderer::previewMaterials(node);
}

IAssetViewer *createEngineAssetViewer(const std::shared_ptr<Engine> &engine,
                                      EngineRenderDriver *driver, QWidget *parent)
{
    return new EngineAssetViewer(engine, driver, parent);
}
