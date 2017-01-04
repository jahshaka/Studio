/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "mainwindow.h"
//#include "ui_mainwindow.h"
#include "ui_newmainwindow.h"

#include <qwindow.h>
#include <qsurface.h>

#include <QOpenGLContext>
#include <qstandarditemmodel.h>
#include <QKeyEvent>
#include <QMessageBox>

#include <QFileDialog>

#include <QTreeWidgetItem>

#include <QTimer>
#include <math.h>
#include <QDesktopServices>

#include "dialogs/loadmeshdialog.h"
#include "core/surfaceview.h"
#include "core/nodekeyframeanimation.h"
#include "core/nodekeyframe.h"
#include "globals.h"

#include "io/scenewriter.h"
#include "io/scenereader.h"

#include "widgets/animationwidget.h"

#include "dialogs/renamelayerdialog.h"
#include "widgets/layertreewidget.h"
#include "core/project.h"
#include "widgets/accordianbladewidget.h"

#include "editor/editorcameracontroller.h"
#include "core/settingsmanager.h"
#include "dialogs/preferencesdialog.h"
#include "dialogs/preferences/worldsettings.h"
#include "dialogs/licensedialog.h"
#include "dialogs/aboutdialog.h"

#include "helpers/collisionhelper.h"

#include "widgets/sceneviewwidget.h"
//#include "irisgl/src/irisgl.h"
#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/scenegraph/lightnode.h"
#include "irisgl/src/materials/defaultmaterial.h"
#include "irisgl/src/graphics/forwardrenderer.h"
#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/graphics/shader.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/graphics/viewport.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/animation/keyframeset.h"
#include "irisgl/src/animation/keyframeanimation.h"

#include "core/materialpreset.h"

enum class VRButtonMode : int
{
    Default = 0,
    Disabled,
    VRMode
};

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    //ui(new Ui::MainWindow)
    ui(new Ui::NewMainWindow)
{
    ui->setupUi(this);
    this->setWindowTitle("Jahshaka VR");

    settings = SettingsManager::getDefaultManager();
    prefsDialog = new PreferencesDialog(settings);
    aboutDialog = new AboutDialog();
    licenseDialog = new LicenseDialog();

    ui->mainTimeline->setMainWindow(this);
    ui->modelpresets->setMainWindow(this);
    ui->materialpresets->setMainWindow(this);

    camControl = nullptr;
    vrMode = false;

    setupFileMenu();
    setupViewMenu();
    setupHelpMenu();

    this->setupLayerButtonMenu();

    // TIMER
    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateAnim()));

    setProjectTitle(Globals::project->getProjectName());

    // initialzie scene view
    sceneView = new SceneViewWidget(this);
    sceneView->setParent(this);

    QGridLayout* layout = new QGridLayout(ui->sceneContainer);
    layout->addWidget(sceneView);
    layout->setMargin(0);
    ui->sceneContainer->setLayout(layout);

    connect(sceneView, SIGNAL(initializeGraphics(SceneViewWidget*, QOpenGLFunctions_3_2_Core*)),
            this, SLOT(initializeGraphics(SceneViewWidget*, QOpenGLFunctions_3_2_Core*)));

    ui->sceneHierarchy->setMainWindow(this);

    connect(ui->sceneHierarchy,SIGNAL(sceneNodeSelected(QSharedPointer<iris::SceneNode>)),
            this, SLOT(sceneNodeSelected(QSharedPointer<iris::SceneNode>)));

    connect(sceneView, SIGNAL(sceneNodeSelected(QSharedPointer<iris::SceneNode>)),
            this, SLOT(sceneNodeSelected(QSharedPointer<iris::SceneNode>)));

    connect(ui->cameraTypeCombo, SIGNAL(currentTextChanged(QString)),
            this, SLOT(cameraTypeChanged(QString)));

    connect(ui->transformCombo, SIGNAL(currentTextChanged(QString)),
            this, SLOT(transformOrientationChanged(QString)));
}

void MainWindow::setupVrUi()
{
    ui->vrBtn->setToolTipDuration(0);
    if(sceneView->isVrSupported())
    {
        ui->vrBtn->setEnabled(true);
        ui->vrBtn->setToolTip("Press to view the scene in vr");
        ui->vrBtn->setProperty("vrMode",(int)VRButtonMode::Default);
    }
    else
    {
        ui->vrBtn->setEnabled(false);
        ui->vrBtn->setToolTip("No Oculus device detected");
        ui->vrBtn->setProperty("vrMode",(int)VRButtonMode::Disabled);
    }

    connect(ui->vrBtn,SIGNAL(clicked(bool)),SLOT(vrButtonClicked(bool)));

    //needed to apply changes
    ui->vrBtn->style()->unpolish(ui->vrBtn);
    ui->vrBtn->style()->polish(ui->vrBtn);
}

/**
 * uses style property trick
 * http://wiki.qt.io/Dynamic_Properties_and_Stylesheets
 */
void MainWindow::vrButtonClicked(bool)
{
    if(!sceneView->isVrSupported())
    {
    }
    else
    {

        if(sceneView->getViewportMode()==ViewportMode::Editor)
        {
            sceneView->setViewportMode(ViewportMode::VR);

            //highlight button blue
            ui->vrBtn->setProperty("vrMode",(int)VRButtonMode::VRMode);
        }
        else

        {
            sceneView->setViewportMode(ViewportMode::Editor);

            //return button back to normal color
            ui->vrBtn->setProperty("vrMode",(int)VRButtonMode::Default);
        }
    }


    //needed to apply changes
    ui->vrBtn->style()->unpolish(ui->vrBtn);
    ui->vrBtn->style()->polish(ui->vrBtn);
}

QString MainWindow::getAbsolutePath(QString relToApp)
{
    auto path = QDir::cleanPath(QCoreApplication::applicationDirPath()+QDir::separator()+relToApp);
    qDebug()<<path;
    return path;
}

//create test scene
void MainWindow::initializeGraphics(SceneViewWidget* widget,QOpenGLFunctions_3_2_Core* gl)
{
    auto scene = iris::Scene::create();

    auto cam = iris::CameraNode::create();
    cam->pos = QVector3D(6, 12, 14);

    scene->setCamera(cam);
    // editor camera shouldnt be a part of the scene itself
    // scene->rootNode->addChild(cam);

    scene->setSkyColor(QColor(64, 64, 64, 255));

    // second node
    auto node = iris::MeshNode::create();
    node->setMesh(getAbsolutePath("../app/models/plane.obj"));
    node->scale = QVector3D(1024, 1, 1024);
    node->setName("Ground");

    auto m = iris::DefaultMaterial::create();
    node->setMaterial(m);
    m->setDiffuseColor(QColor(255, 255, 255));
    m->setDiffuseTexture(iris::Texture2D::load(getAbsolutePath("../app/content/textures/tile.png")));
    m->setShininess(0);
    m->setSpecularColor(QColor(0, 0, 0));
    m->setTextureScale(500);
    scene->rootNode->addChild(node);

    // add test object with basic material
    auto boxNode = iris::MeshNode::create();
    boxNode->setMesh(getAbsolutePath("../assets/models/StanfordLucy.obj"));
    boxNode->setName("Stanford Lucy");
    boxNode->scale = QVector3D(2, 2, 2);

    auto mat = iris::DefaultMaterial::create();
    boxNode->setMaterial(mat);
    mat->setDiffuseColor(QColor(156, 170, 206));
    mat->setDiffuseTexture(iris::Texture2D::load(getAbsolutePath("../assets/textures/TexturesCom_MarbleWhite0058_M.jpg")));
    mat->setShininess(2);
    //mat->setAmbientColor(QColor(64, 64, 64));

    // lighting
    auto light = iris::LightNode::create();
    light->setLightType(iris::LightType::Point);
    scene->rootNode->addChild(light);
    light->setName("Bounce Lamp");
    light->pos = QVector3D(-3, 7, 5);
    light->intensity = .21;
    light->icon = iris::Texture2D::load("app/icons/bulb.png");

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    scene->rootNode->addChild(dlight);
    dlight->setName("Main Lamp");
    dlight->pos = QVector3D(0, 10, 0);
    dlight->intensity = 1;
    dlight->icon = iris::Texture2D::load("app/icons/bulb.png");


    scene->rootNode->addChild(boxNode);

    // fog params
    scene->fogColor = QColor(64, 64, 64, 255);

    this->setScene(scene);
    setupVrUi();
}

void MainWindow::cameraTypeChanged(QString type)
{
    if (type == "Free") {
        sceneView->setFreeCameraMode();
    } else {
        sceneView->setArcBallCameraMode();
    }
}

void MainWindow::transformOrientationChanged(QString type)
{
    if (type == "Local") {
        sceneView->setTransformOrientationLocal();
    } else {
        sceneView->setTransformOrientationGlobal();
    }
}

void MainWindow::setSettingsManager(SettingsManager* settings)
{
    this->settings = settings;
}

SettingsManager* MainWindow::getSettingsManager()
{
    return settings;
}

bool MainWindow::handleMousePress(QMouseEvent *event)
{
    mouseButton = event->button();
    mousePressPos = event->pos();

    //if(event->button() == Qt::LeftButton)
    //    gizmo->onMousePress(event->x(),event->y());

    return true;
}

bool MainWindow::handleMouseRelease(QMouseEvent *event)
{
    return true;
}

bool MainWindow::handleMouseMove(QMouseEvent *event)
{
    mousePos = event->pos();

    //gizmo->onMouseMove(event->x(),event->y());

    return false;
}

//todo: disable scrolling while doing gizmo transform
bool MainWindow::handleMouseWheel(QWheelEvent *event)
{
    return false;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    Q_UNUSED(obj)

    switch (event->type()) {
    case QEvent::KeyPress: {
        break;
    }
    case QEvent::MouseButtonPress:
        if (obj == surface)
            return handleMousePress(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseButtonRelease:
        if (obj == surface)
            return handleMouseRelease(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::MouseMove:
        if (obj == surface)
            return handleMouseMove(static_cast<QMouseEvent *>(event));
        break;
    case QEvent::Wheel:
        if (obj == surface)
            return handleMouseWheel(static_cast<QWheelEvent *>(event));
        break;
    default:
        break;
    }

    return false;
}

void MainWindow::setupFileMenu()
{
    // add recent files
    auto recent = settings->getRecentlyOpenedScenes();

    if (recent.size() == 0) {
        ui->menuOpenRecent->setEnabled(false);
    } else {
        ui->menuOpenRecent->setEnabled(true);
        ui->menuOpenRecent->clear();

        for (auto item : recent) {
            auto action = new QAction(item, ui->menuOpenRecent);
            action->setData(item);
            connect(action,SIGNAL(triggered(bool)), this, SLOT(openRecentFile()));
            ui->menuOpenRecent->addAction(action);
        }
    }

    connect(ui->actionSave,         SIGNAL(triggered(bool)), this, SLOT(saveScene()));
    connect(ui->actionSave_As,      SIGNAL(triggered(bool)), this, SLOT(saveSceneAs()));
    connect(ui->actionLoad,         SIGNAL(triggered(bool)), this, SLOT(loadScene()));
    connect(ui->actionExit,         SIGNAL(triggered(bool)), this, SLOT(exitApp()));
    connect(ui->actionPreferences,  SIGNAL(triggered(bool)), this, SLOT(showPreferences()));
    connect(ui->actionNew,          SIGNAL(triggered(bool)), this, SLOT(newScene()));

    connect(prefsDialog,  SIGNAL(PreferencesDialogClosed()), this, SLOT(updateSceneSettings()));
}

void MainWindow::setupViewMenu()
{
    connect(ui->actionEditorCamera, SIGNAL(triggered(bool)), this, SLOT(useEditorCamera()));
    connect(ui->actionViewerCamera, SIGNAL(triggered(bool)), this, SLOT(useUserCamera()));
}

void MainWindow::setupHelpMenu()
{
    connect(ui->actionLicense,      SIGNAL(triggered(bool)), this, SLOT(showLicenseDialog()));
    connect(ui->actionAbout,        SIGNAL(triggered(bool)), this, SLOT(showAboutDialog()));
    connect(ui->actionBlog,         SIGNAL(triggered(bool)), this, SLOT(openBlogUrl()));
    connect(ui->actionOpenWebsite,  SIGNAL(triggered(bool)), this, SLOT(openWebsiteUrl()));
}

void MainWindow::setProjectTitle(QString projectTitle)
{
    this->setWindowTitle(projectTitle + " - Jahshaka");
}

void MainWindow::sceneTreeCustomContextMenu(const QPoint& pos)
{
}

void MainWindow::renameNode()
{
    RenameLayerDialog dialog(this);
    dialog.setName(activeSceneNode->getName());
    dialog.exec();

    activeSceneNode->setName(dialog.getName());
    //item->setText(0,node->getName());

    //for now
    this->ui->sceneHierarchy->repopulateTree();

}

void MainWindow::updateGizmoTransform()
{
    //if(activeSceneNode==nullptr)
    //    return;
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);

    float aspectRatio = ui->sceneContainer->width()/(float)ui->sceneContainer->height();
    //editorCam->lens()->setPerspectiveProjection(45.0f, aspectRatio, 0.1f, 1000.0f);
}

void MainWindow::stopAnimWidget()
{
    animWidget->stopAnimation();
}

void MainWindow::saveScene()
{

    if(Globals::project->isSaved())
    {
        auto filename = Globals::project->getFilePath();
        auto writer = new SceneWriter();
        writer->writeScene(filename,scene);

        settings->addRecentlyOpenedScene(filename);

        delete writer;
    }
    else
    {
        auto filename = QFileDialog::getSaveFileName(this,"Save Scene","","Jashaka Scene (*.jah)");
        auto writer = new SceneWriter();
        writer->writeScene(filename,scene);

        Globals::project->setFilePath(filename);
        this->setProjectTitle(Globals::project->getProjectName());

        settings->addRecentlyOpenedScene(filename);

        delete writer;
    }

}

void MainWindow::saveSceneAs()
{

    QString dir = QApplication::applicationDirPath()+"/scenes/";
    auto filename = QFileDialog::getSaveFileName(this,"Save Scene",dir,"Jashaka Scene (*.jah)");
    auto writer = new SceneWriter();
    writer->writeScene(filename,scene);

    Globals::project->setFilePath(filename);
    this->setProjectTitle(Globals::project->getProjectName());

    settings->addRecentlyOpenedScene(filename);

    delete writer;
}

void MainWindow::loadScene()
{
    QString dir = QApplication::applicationDirPath()+"/scenes/";
    auto filename = QFileDialog::getOpenFileName(this,"Open Scene File",dir,"Jashaka Scene (*.jah)");

    if(filename.isEmpty() || filename.isNull())
        return;

    openProject(filename);
}

void MainWindow::openProject(QString filename)
{

    //remove current scene first
    this->removeScene();

    //load new scene
    auto reader = new SceneReader();

    auto scene = reader->readScene(filename);
    //auto sceneEnt = new Qt3DCore::QEntity();
    //sceneEnt->setParent(rootEntity);
    //auto scene = exp->loadScene(filename,sceneEnt);
    setScene(scene);

    Globals::project->setFilePath(filename);
    this->setProjectTitle(Globals::project->getProjectName());

    settings->addRecentlyOpenedScene(filename);

    delete reader;

}

void MainWindow::applyMaterialPreset(MaterialPreset* preset)
{
    if(!activeSceneNode || activeSceneNode->sceneNodeType!=iris::SceneNodeType::Mesh)
        return;

    auto meshNode = activeSceneNode.staticCast<iris::MeshNode>();

    auto mat = iris::DefaultMaterial::create();
    mat->setAmbientColor(preset->ambientColor);

    mat->setDiffuseColor(preset->diffuseColor);
    mat->setDiffuseTexture(iris::Texture2D::load(preset->diffuseTexture));

    mat->setSpecularColor(preset->specularColor);
    mat->setSpecularTexture(iris::Texture2D::load(preset->specularTexture));
    mat->setShininess(preset->shininess);

    mat->setNormalTexture(iris::Texture2D::load(preset->normalTexture));
    mat->setNormalIntensity(preset->normalIntensity);

    mat->setReflectionTexture(iris::Texture2D::load(preset->reflectionTexture));
    mat->setReflectionInfluence(preset->reflectionInfluence);

    meshNode->setMaterial(mat);

    //todo: update node's material without updating the whole ui
    this->ui->sceneNodeProperties->refreshMaterial();
}

/**
 * @brief callback for "Recent Files" submenu actions
 * opens files directly, no file dialog
 * shows dialog box showing error if file doesnt exist anymore
 * @todo request if file should be saved
 * @param action
 */
void MainWindow::openRecentFile()
{
    auto action = qobject_cast<QAction*>(sender());
    auto filename = action->data().toString();
    QFileInfo info(filename);

    if(!info.exists())
    {
        QMessageBox box(this);
        box.setText("unable not locate file '"+filename+"'");
        box.exec();
        return;
    }

    Globals::project->setFilePath(filename);
    this->setProjectTitle(Globals::project->getProjectName());

    settings->addRecentlyOpenedScene(filename);
}

void MainWindow::setScene(QSharedPointer<iris::Scene> scene)
{
    this->scene = scene;
    this->sceneView->setScene(scene);
    ui->sceneHierarchy->setScene(scene);

    // interim...
    updateSceneSettings();
}

void MainWindow::removeScene()
{
}

void MainWindow::setupPropertyUi()
{
    //ANIMATION
    animWidget = new AnimationWidget();
    animWidget->setMainTimelineWidget(ui->mainTimeline->getTimeline());

    //WORLD
    //worldLayerWidget = new WorldLayerWidget();
}

void MainWindow::sceneNodeSelected(QTreeWidgetItem* item)
{
    //auto data = (SceneNode*)item->data(1,Qt::UserRole).value<void*>();
    //setActiveSceneNode(data);
}

void MainWindow::sceneTreeItemChanged(QTreeWidgetItem* item,int column)
{

}

void MainWindow::sceneNodeSelected(QSharedPointer<iris::SceneNode> sceneNode)
{
    //show properties for scenenode
    activeSceneNode = sceneNode;

    sceneView->setSelectedNode(sceneNode);
    ui->sceneNodeProperties->setSceneNode(sceneNode);
    ui->sceneHierarchy->setSelectedNode(sceneNode);
    ui->animationtimeline->setSceneNode(sceneNode);
}


void MainWindow::setupLayerButtonMenu()
{
}

void MainWindow::updateAnim()
{
    //scene->getRootNode()->update(1/60.0f);
    //scene->getRootNode()->applyAnimationAtTime(Globals::animFrameTime);
}

void MainWindow::setSceneAnimTime(float time)
{
    //scene->getRootNode()->applyAnimationAtTime(time);
}

/**
 * Adds a cube to the current scene
 */
void MainWindow::addCube()
{
    auto node = iris::MeshNode::create();
    node->setMesh("app/content/primitives/cube.obj");
    node->setName("Cube");

    addNodeToScene(node);
}

/**
 * Adds a torus to the current scene
 */
void MainWindow::addTorus()
{
    auto node = iris::MeshNode::create();
    node->setMesh("app/content/primitives/torus.obj");
    node->setName("Torus");

    addNodeToScene(node);
}

/**
 * Adds a sphere to the current scene
 */
void MainWindow::addSphere()
{
    auto node = iris::MeshNode::create();
    node->setMesh("app/content/primitives/sphere.obj");
    node->setName("Sphere");

    addNodeToScene(node);
}

/**
 * Adds a cylinder to the current scene
 */
void MainWindow::addCylinder()
{
    auto node = iris::MeshNode::create();
    node->setMesh("app/content/primitives/cylinder.obj");
    node->setName("Cylinder");

    addNodeToScene(node);
}

void MainWindow::addPointLight()
{
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Point);
    node->icon = iris::Texture2D::load("app/icons/bulb.png");

    addNodeToScene(node);
}

void MainWindow::addSpotLight()
{
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Spot);
    node->icon = iris::Texture2D::load("app/icons/bulb.png");

    addNodeToScene(node);
}

void MainWindow::addDirectionalLight()
{
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Directional);
    node->icon = iris::Texture2D::load("app/icons/bulb.png");

    addNodeToScene(node);
}

void MainWindow::addMesh()
{
    QString dir = QApplication::applicationDirPath()+"/assets/models/";
    //qDebug()<<dir;
    auto filename = QFileDialog::getOpenFileName(this,"Load Mesh",dir,"Mesh Files (*.obj *.fbx *.3ds)");
    auto nodeName = QFileInfo(filename).baseName();
    if(filename.isEmpty())
        return;


    //auto node = iris::MeshNode::create();
    //node->setMesh(filename);
    //node->setName(nodeName);

    auto node = iris::MeshNode::loadAsSceneFragment(filename);
    node->setName(nodeName);

    //todo: load material data
    addNodeToScene(node);
}

void MainWindow::addViewPoint()
{
    //addSceneNodeToSelectedTreeItem(ui->sceneTree,new UserCameraNode(new Qt3DCore::QEntity()),true,getIconFromSceneNodeType(SceneNodeType::UserCamera));
}

void MainWindow::addTexturedPlane()
{
    /*
    auto node = TexturedPlaneNode::createTexturedPlane("Textured Plane");
    //node->setTexture(path);
    addSceneNodeToSelectedTreeItem(ui->sceneTree,node,false,QIcon(":app/icons/square.svg"));
    */
}

/**
 * Adds sceneNode to selected scene node. If there is no selected scene node,
 * sceneNode is added to the root node
 * @param sceneNode
 */
void MainWindow::addNodeToActiveNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if(!scene)
    {
        //todo: set alert that a scene needs to be set before this can be done
    }

    //apply default material
    if(sceneNode->sceneNodeType == iris::SceneNodeType::Mesh)
    {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();

        if(!meshNode->getMaterial())
        {
            auto mat = iris::DefaultMaterial::create();
            meshNode->setMaterial(mat);
        }

    }

    if(!!activeSceneNode)
    {
        activeSceneNode->addChild(sceneNode);
    }
    else
    {
        scene->getRootNode()->addChild(sceneNode);
    }

    ui->sceneHierarchy->repopulateTree();
}

/**
 * adds sceneNode directly to the scene's rootNode
 * applied default material to mesh if one isnt present
 */
void MainWindow::addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode)
{
    if(!scene)
    {
        //todo: set alert that a scene needs to be set before this can be done
        return;
    }

    //apply default material
    if(sceneNode->sceneNodeType == iris::SceneNodeType::Mesh)
    {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();

        if(!meshNode->getMaterial())
        {
            auto mat = iris::DefaultMaterial::create();
            meshNode->setMaterial(mat);
        }

    }

    scene->getRootNode()->addChild(sceneNode);

    ui->sceneHierarchy->repopulateTree();
}

void MainWindow::duplicateNode()
{

}

void MainWindow::deleteNode()
{
    if(!!activeSceneNode)
    {
        activeSceneNode->removeFromParent();
        ui->sceneHierarchy->repopulateTree();
        sceneView->clearSelectedNode();
        ui->sceneNodeProperties->setSceneNode(QSharedPointer<iris::SceneNode>(nullptr));
    }

    /*
    auto items = ui->sceneTree->selectedItems();
    if(items.size()==0)
        return;
    auto item = items[0];

    auto node = (SceneNode*)item->data(1,Qt::UserRole).value<void*>();

    //world node isnt removable
    if(!node->isRemovable())
        return;

    item->parent()->removeChild(item);
    node->getParent()->removeChild(node);
    //sceneNodes.remove(node->getEntity()->id());
    sceneTreeItems.remove(node->getEntity()->id());

    ui->tabWidget->clear();
    */
}


void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    event->acceptProposedAction();
}

void MainWindow::dragMoveEvent(QDragMoveEvent *event)
{
    event->acceptProposedAction();
}

/**
 * @brief accepts model files dropped into scene
 * currently only .obj files are supported
 */
void MainWindow::dropEvent(QDropEvent* event)
{
    /*
    const QMimeData* mimeData = event->mimeData();

    if (mimeData->hasUrls())
    {
        QStringList pathList;
        QList<QUrl> urlList = mimeData->urls();

        // extract the local paths of the files
        for (int i = 0; i < urlList.size(); i++)
        {
            QString filename = urlList.at(i).toLocalFile();

            //only use .obj files for now
            QFileInfo fileInfo(filename);
            qDebug()<<fileInfo.completeSuffix();
            if(fileInfo.completeSuffix()!="obj")
                continue;

            auto nodeName = fileInfo.baseName();

            auto node = MeshNode::loadMesh(nodeName,filename);

            auto mat = new AdvanceMaterial();
            node->setMaterial(mat);

            addSceneNodeToSelectedTreeItem(ui->sceneTree,node,false,getIconFromSceneNodeType(node->sceneNodeType));
        }

    }

    event->acceptProposedAction();
    */
}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}


QIcon MainWindow::getIconFromSceneNodeType(SceneNodeType type)
{
    /*
    switch(type)
    {
        case SceneNodeType::Cube:
        case SceneNodeType::Cylinder:
        case SceneNodeType::Mesh:
        case SceneNodeType::Sphere:
        case SceneNodeType::Torus:
            return QIcon(":app/icons/sceneobject.svg");
        case SceneNodeType::Light:
            return QIcon(":app/icons/light.svg");
        case SceneNodeType::World:
            return QIcon(":app/icons/world.svg");
        case SceneNodeType::UserCamera:
            return QIcon(":app/icons/people.svg");
    default:
        return QIcon();
    }
    */

    return QIcon();
}

void MainWindow::showPreferences()
{
    prefsDialog->exec();
}

void MainWindow::exitApp()
{
    QApplication::exit();
}

void MainWindow::updateSceneSettings()
{
    scene->setOutlineWidth(prefsDialog->worldSettings->outlineWidth);
    scene->setOutlineColor(prefsDialog->worldSettings->outlineColor);
}

void MainWindow::newScene()
{

}

void MainWindow::showAboutDialog()
{
    aboutDialog->exec();
}

void MainWindow::showLicenseDialog()
{
    licenseDialog->exec();
}

void MainWindow::openBlogUrl()
{
    QDesktopServices::openUrl(QUrl("http://www.jahshaka.com/category/blog/"));
}

void MainWindow::openWebsiteUrl()
{
    QDesktopServices::openUrl(QUrl("http://www.jahshaka.com/"));
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::on_translateGizmoBtn_clicked()
{
    sceneView->setGizmoLoc();
}

void MainWindow::on_scaleGizmoBtn_clicked()
{
    sceneView->setGizmoScale();
}

void MainWindow::on_rotateGizmoBtn_clicked()
{
    sceneView->setGizmoRot();
}
