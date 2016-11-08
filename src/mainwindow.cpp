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
//#include "editorcameracontroller.h"

//#include "widgets/lightlayerwidget.h"
#include "widgets/animationwidget.h"

#include "widgets/materialwidget.h"
//#include "core/sceneparser.h"
//#include "transformgizmo.h"
//#include "editor/gizmos/advancedtransformgizmo.h"
#include "dialogs/renamelayerdialog.h"
#include "widgets/layertreewidget.h"
#include "core/project.h"

#include "editor/editorcameracontroller.h"
#include "core/settingsmanager.h"
#include "dialogs/preferencesdialog.h"
#include "dialogs/licensedialog.h"
#include "dialogs/aboutdialog.h"

#include "helpers/collisionhelper.h"

#include "widgets/sceneviewwidget.h"
#include "jah3d/scenegraph/meshnode.h"
#include "jah3d/scenegraph/cameranode.h"
#include "jah3d/materials/defaultmaterial.h"

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
    camControl = nullptr;

    //todo: bind callbacks
    QMenu* addMenu = new QMenu();

    auto primtiveMenu = addMenu->addMenu("Primtives");
    QAction *action = new QAction("Torus", this);
    primtiveMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addTorus()));

    action = new QAction("Cube", this);
    primtiveMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addCube()));

    action = new QAction("Sphere", this);
    primtiveMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addSphere()));

    action = new QAction("Cylinder", this);
    primtiveMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addCylinder()));

    action = new QAction("Plane", this);
    primtiveMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addTexturedPlane()));

    //LIGHTS
    auto lightMenu = addMenu->addMenu("Lights");
    action = new QAction("PointLight", this);
    lightMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addPointLight()));

    action = new QAction("SpotLight", this);
    lightMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addSpotLight()));

    action = new QAction("DirectionalLight", this);
    lightMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addDirectionalLight()));

    connect(ui->deleteButton,SIGNAL(pressed()),this,SLOT(deleteNode()));

    setupFileMenu();
    setupViewMenu();
    setupHelpMenu();

    //MESHES
    action = new QAction("Load 3D Object", this);
    addMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addMesh()));

    //VIEWPOINT
    action = new QAction("ViewPoint", this);
    addMenu->addAction(action);
    connect(action,SIGNAL(triggered()),this,SLOT(addViewPoint()));

    //resize event for plane
    ui->toolButton->setMenu(addMenu);
    ui->toolButton->setPopupMode(QToolButton::InstantPopup);

    //SCENE TREE
    connect(ui->sceneTree,SIGNAL(itemClicked(QTreeWidgetItem*,int)),this,SLOT(sceneNodeSelected(QTreeWidgetItem*)));
    connect(ui->sceneTree,SIGNAL(itemChanged(QTreeWidgetItem*,int)),this,SLOT(sceneTreeItemChanged(QTreeWidgetItem*,int)));

    //make items draggable and droppable
    ui->sceneTree->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->sceneTree->setDragEnabled(true);
    ui->sceneTree->viewport()->setAcceptDrops(true);
    ui->sceneTree->setDropIndicatorShown(true);
    ui->sceneTree->setDragDropMode(QAbstractItemView::InternalMove);

    //custom context menu
    //http://stackoverflow.com/questions/22198427/adding-a-right-click-menu-for-specific-items-in-qtreeview
    ui->sceneTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->sceneTree, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(sceneTreeCustomContextMenu(const QPoint &)));

    this->setupLayerButtonMenu();

    //TIMER
    timer = new QTimer(this);
    connect(timer,SIGNAL(timeout()),this,SLOT(updateAnim()));

    //activeSceneNode = nullptr;

    this->rebuildTree();
    setProjectTitle(Globals::project->getProjectName());

    //init scene view
    sceneView = new SceneViewWidget(this);
    sceneView->setParent(this);

    //
    QGridLayout* layout = new QGridLayout();
    layout->addWidget(sceneView);
    layout->setMargin(0);
    ui->sceneContainer->setLayout(layout);

    //createTestScene();
}

void MainWindow::createTestScene()
{
    auto scene = jah3d::Scene::create();

    auto cam = jah3d::CameraNode::create();
    //cam->lookAt(QVector3D(0,0,0),QVect);
    scene->setCamera(cam);

    //add test object with basic material
    auto obj = jah3d::MeshNode::create();
    obj->setMesh("app/models/head.obj");
    obj->setMaterial(jah3d::DefaultMaterial::create());

    scene->rootNode->addChild(obj);
    sceneView->setScene(scene);
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
    //if(activeGizmoHandle!=nullptr)
    //    activeGizmoHandle->removeHighlight();

    /*
    mouseButton = event->button();
    mouseReleasePos = event->pos();



    //show context menu
    if(mouseButton == Qt::RightButton
            && (mouseReleasePos - mousePressPos).manhattanLength() < 3 //mouse press should be equal to mouse release
            && activeSceneNode != nullptr
            )
    {
        QMenu menu;
        createSceneNodeContextMenu(menu,activeSceneNode);

        menu.exec(mouseReleasePos);
    }
    else if(mouseButton == Qt::LeftButton)
    {
        gizmo->onMouseRelease(event->x(),event->y());
    }
    */
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
    /*
    auto speed = 0.01f;
    this->camControl->getCamera()->translate(QVector3D(0,0,event->delta()*speed));
    */
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
    //add recent files
    auto recent = settings->getRecentlyOpenedScenes();
    if(recent.size()==0)
    {
        ui->menuOpenRecent->setEnabled(false);
    }
    else
    {
        ui->menuOpenRecent->setEnabled(true);

        ui->menuOpenRecent->clear();
        for(auto item:recent)
        {
            //if(item.size()>20)
            //    item = item;
            auto action = new QAction(item,ui->menuOpenRecent);
            action->setData(item);
            connect(action,SIGNAL(triggered(bool)),this,SLOT(openRecentFile()));
            ui->menuOpenRecent->addAction(action);
        }
    }


    connect(ui->actionSave,SIGNAL(triggered(bool)),this,SLOT(saveScene()));
    connect(ui->actionSave_As,SIGNAL(triggered(bool)),this,SLOT(saveSceneAs()));
    connect(ui->actionLoad,SIGNAL(triggered(bool)),this,SLOT(loadScene()));
    connect(ui->actionExit,SIGNAL(triggered(bool)),this,SLOT(exitApp()));
    connect(ui->actionPreferences,SIGNAL(triggered(bool)),this,SLOT(showPreferences()));
    connect(ui->actionNew,SIGNAL(triggered(bool)),this,SLOT(newScene()));

}

void MainWindow::setupViewMenu()
{
    connect(ui->actionEditorCamera,SIGNAL(triggered(bool)),this,SLOT(useEditorCamera()));
    connect(ui->actionViewerCamera,SIGNAL(triggered(bool)),this,SLOT(useUserCamera()));
}

void MainWindow::setupHelpMenu()
{
    connect(ui->actionLicense,SIGNAL(triggered(bool)),this,SLOT(showLicenseDialog()));
    connect(ui->actionAbout,SIGNAL(triggered(bool)),this,SLOT(showAboutDialog()));
    connect(ui->actionBlog,SIGNAL(triggered(bool)),this,SLOT(openBlogUrl()));
    connect(ui->actionVisit_Website,SIGNAL(triggered(bool)),this,SLOT(openWebsiteUrl()));
}

void MainWindow::setProjectTitle(QString projectTitle)
{
    this->setWindowTitle(projectTitle+" - Jahshaka");
}

void MainWindow::sceneTreeCustomContextMenu(const QPoint& pos)
{
    QModelIndex index = ui->sceneTree->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    auto item = ui->sceneTree->itemAt(pos);
    auto node = (SceneNode*)item->data(1,Qt::UserRole).value<void*>();
    QMenu menu;
    QAction* action;

    //rename
    action = new QAction(QIcon(),"Rename",this);
    connect(action,SIGNAL(triggered()),this,SLOT(renameNode()));
    menu.addAction(action);

    /*
    //world node isnt removable
    if(node->isRemovable())
    {
        action = new QAction(QIcon(),"Delete",this);
        connect(action,SIGNAL(triggered()),this,SLOT(deleteNode()));
        menu.addAction(action);
    }

    if(node->isDuplicable())
    {
        action = new QAction(QIcon(),"Duplicate",this);
        connect(action,SIGNAL(triggered()),this,SLOT(duplicateNode()));
        menu.addAction(action);
    }
    */

    menu.exec(ui->sceneTree->mapToGlobal(pos));
}

void MainWindow::renameNode()
{
    /*
    auto items = ui->sceneTree->selectedItems();
    if(items.size()==0)
        return;
    auto item = items[0];

    auto node = (SceneNode*)item->data(1,Qt::UserRole).value<void*>();

    RenameLayerDialog dialog(this);
    dialog.setName(node->getName());
    dialog.exec();

    node->setName(dialog.getName());
    item->setText(0,node->getName());
    */
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
/*
void MainWindow::useEditorCamera()
{
    forwardRenderer->setCamera(editorCam);

    auto cam = (UserCameraNode*)this->scene->getRootNode()->children[0];
    cam->showBody();
}
*/
/*
void MainWindow::useUserCamera()
{
    auto cam = (UserCameraNode*)this->scene->getRootNode()->children[0];
    forwardRenderer->setCamera(editorCam);
    cam->hideBody();
}
*/

void MainWindow::saveScene()
{
    /*
    if(Globals::project->isSaved())
    {
        auto filename = Globals::project->getFilePath();
        auto exp = new SceneParser();
        exp->saveScene(filename,scene);

        settings->addRecentlyOpenedScene(filename);
    }
    else
    {
        auto filename = QFileDialog::getSaveFileName(this,"Save Scene","","Jashaka Scene (*.jah)");
        auto exp = new SceneParser();
        exp->saveScene(filename,scene);

        Globals::project->setFilePath(filename);
        this->setProjectTitle(Globals::project->getProjectName());

        settings->addRecentlyOpenedScene(filename);
    }
    */
}

void MainWindow::saveSceneAs()
{
    /*
    QString dir = QApplication::applicationDirPath()+"/scenes/";
    auto filename = QFileDialog::getSaveFileName(this,"Save Scene",dir,"Jashaka Scene (*.jah)");
    auto exp = new SceneParser();
    exp->saveScene(filename,scene);

    Globals::project->setFilePath(filename);
    this->setProjectTitle(Globals::project->getProjectName());

    settings->addRecentlyOpenedScene(filename);
    */
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
    /*
    //remove current scene first
    this->removeScene();

    //load new scene
    auto exp = new SceneParser();

    //auto scene = exp->loadScene(filename,new Qt3DCore::QEntity());
    //auto sceneEnt = new Qt3DCore::QEntity();
    //sceneEnt->setParent(rootEntity);
    //auto scene = exp->loadScene(filename,sceneEnt);
    //setScene(scene);

    Globals::project->setFilePath(filename);
    this->setProjectTitle(Globals::project->getProjectName());

    settings->addRecentlyOpenedScene(filename);

    delete exp;
    */
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

    //auto exp = new SceneParser();
    //auto sceneEnt = new Qt3DCore::QEntity();
    //sceneEnt->setParent(rootEntity);

    //auto scene = exp->loadScene(filename,sceneEnt);
    //setScene(scene);

    Globals::project->setFilePath(filename);
    this->setProjectTitle(Globals::project->getProjectName());

    settings->addRecentlyOpenedScene(filename);
}

void MainWindow::removeScene()
{
}

void MainWindow::setupPropertyUi()
{
    //TRANSFROMS
    //transformUi = new TransformWidget();

    //LIGHT LAYER
    //lightLayerWidget = new LightLayerWidget();

    //MATERIAL UI
    materialWidget = new MaterialWidget();

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
    /*
    auto node = (SceneNode*)item->data(1,Qt::UserRole).value<void*>();

    if(item->checkState(column) == Qt::Checked)
    {
        node->show();
    }
    else
    {
        node->hide();
    }
    */
}


void MainWindow::setupLayerButtonMenu()
{
    /*
    QMenu* addMenu = new QMenu();

    QAction *action = new QAction("2D Scene", this);
    addMenu->addAction(action);

    action = new QAction("Scene Graph", this);
    addMenu->addAction(action);
    //connect(action,SIGNAL(triggered()),this,SLOT(addSceneGraphLayer()));

    action = new QAction("Text", this);
    addMenu->addAction(action);

    action = new QAction("Particles", this);
    addMenu->addAction(action);

    action = new QAction("Video", this);
    addMenu->addAction(action);
    */
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

void MainWindow::setupQt3d()
{
    /*
    rootEntity = nullptr;

    surface = new SurfaceView();
    surface->installEventFilter(this);
    //QWidget *container = QWidget::createWindowContainer(view,this);
    QWidget *container = QWidget::createWindowContainer(surface,ui->sceneContainer);
    //container->setAcceptDrops(true);

    QGridLayout* layout = new QGridLayout();
    layout->addWidget(container);
    layout->setMargin(0);
    ui->sceneContainer->setLayout(layout);

    engine = new Qt3DCore::QAspectEngine();
    //Qt3D::QAspectEngine engine;
    engine->registerAspect(new Qt3DRender::QRenderAspect());
    engine->registerAspect(new Qt3DLogic::QLogicAspect());
    engine->registerAspect(new Qt3DInput::QInputAspect);

    jahRenderer = new JahRenderer();
    jahRenderer->setSurface(surface);

    renderSettings = new Qt3DRender::QRenderSettings();
    renderSettings->pickingSettings()->setPickMethod(Qt3DRender::QPickingSettings::TrianglePicking);
    renderSettings->pickingSettings()->setPickResultMode(Qt3DRender::QPickingSettings::NearestPick);
    inputSettings = new Qt3DInput::QInputSettings();
    inputSettings->setEventSource(surface);
    //renderSettings->setActiveFrameGraph(forwardRenderer);
    renderSettings->setActiveFrameGraph(jahRenderer);


    rootEntity = new Qt3DCore::QEntity();
    rootEntity->addComponent(renderSettings);
    rootEntity->addComponent(inputSettings);
    engine->setRootEntity(Qt3DCore::QEntityPtr(rootEntity));

    //SCENE
    SceneManager* scene = nullptr;
    auto sceneEnt = new Qt3DCore::QEntity();
    sceneEnt->setParent(rootEntity);

    auto sceneToUse = settings->getValue("default_scene","matrix").toString();
    if(sceneToUse=="grid")
        scene = SceneManager::createDefaultGridScene(sceneEnt);
    else
        scene = SceneManager::createDefaultMatrixScene(sceneEnt);

    this->setScene(scene);
    */
}

void MainWindow::rebuildTree()
{
    /*
    auto sceneRoot = scene->getRootNode();
    auto root = new QTreeWidgetItem();

    root->setText(0,sceneRoot->getName());
    root->setIcon(0,this->getIconFromSceneNodeType(SceneNodeType::World));
    root->setData(1,Qt::UserRole,QVariant::fromValue((void*)sceneRoot));

    //populate tree
    sceneTreeItems.clear();
    populateTree(root,sceneRoot);

    this->ui->sceneTree->clear();
    this->ui->sceneTree->addTopLevelItem(root);
    ui->sceneTree->expandAll();
    */
}

void MainWindow::addCube()
{
    //addSceneNodeToSelectedTreeItem(ui->sceneTree,SceneNode::createCube("Cube"),false,QIcon(":app/icons/sceneobject.svg"));
}

void MainWindow::addTorus()
{
    //addSceneNodeToSelectedTreeItem(ui->sceneTree,TorusNode::createTorus("Torus"),false,QIcon(":app/icons/sceneobject.svg"));
}

void MainWindow::addSphere()
{
    //addSceneNodeToSelectedTreeItem(ui->sceneTree,SphereNode::createSphere("Sphere"),false,QIcon(":app/icons/sceneobject.svg"));
}

void MainWindow::addCylinder()
{
    //addSceneNodeToSelectedTreeItem(ui->sceneTree,CylinderNode::createCylinder("Cylinder"),false,QIcon(":app/icons/sceneobject.svg"));
}

void MainWindow::addPointLight()
{
    //addSceneNodeToSelectedTreeItem(ui->sceneTree,LightNode::createLight("PointLight",LightType::Point),true,QIcon(":app/icons/light.svg"));
}

void MainWindow::addSpotLight()
{
    //addSceneNodeToSelectedTreeItem(ui->sceneTree,LightNode::createLight("SpotLight",LightType::SpotLight),true,QIcon(":app/icons/light.svg"));
}

void MainWindow::addDirectionalLight()
{
    //addSceneNodeToSelectedTreeItem(ui->sceneTree,LightNode::createLight("DirectionalLight",LightType::Directional),true,QIcon(":app/icons/light.svg"));
}

void MainWindow::addMesh()
{
    QString dir = QApplication::applicationDirPath()+"/assets/models/";
    //qDebug()<<dir;
    auto filename = QFileDialog::getOpenFileName(this,"Load Mesh",dir,"Obj Model (*.obj)");
    auto nodeName = QFileInfo(filename).baseName();
    if(filename.isEmpty())
        return;

    /*
    auto node = MeshNode::loadMesh(nodeName,filename);

    auto mat = new AdvanceMaterial();
    node->setMaterial(mat);

    addSceneNodeToSelectedTreeItem(ui->sceneTree,node,false,getIconFromSceneNodeType(node->sceneNodeType));
    */
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

void MainWindow::duplicateNode()
{
    /*
    auto items = ui->sceneTree->selectedItems();
    if(items.size()==0)
        return;
    auto selectedItem = items[0];

    auto node = (SceneNode*)selectedItem->data(1,Qt::UserRole).value<void*>();

    //world node isnt removable
    if(!node->isDuplicable())
        return;

    QTreeWidgetItem* item = nullptr;
    SceneNode* parentNode = nullptr;

    //use rootnode instead
    parentNode = scene->getRootNode();
    //item = ui->sceneTree->invisibleRootItem();
    item = ui->sceneTree->invisibleRootItem()->child(0);

    auto newNode = node->duplicate();
    newNode->setName(node->getName());
    parentNode->addChild(newNode);
    //newNode->pos.setY(3);
    newNode->_updateTransform();
    auto newNodeEnt = newNode->getEntity();

    //tree node
    auto newItem = new QTreeWidgetItem();
    newItem->setText(0,newNode->getName());
    newItem->setData(1,Qt::UserRole,QVariant::fromValue((void*)newNode));
    //newItem->setIcon(0,icon);//this should be changed based on the type of node
    newItem->setFlags(newItem->flags() | Qt::ItemIsUserCheckable);
    newItem->setCheckState(0,Qt::Checked);
    item->addChild(newItem);

    ui->sceneTree->expandAll();
    this->setActiveSceneNode(newNode);

    //select item
    items = ui->sceneTree->selectedItems();
    for(auto i:items)
        ui->sceneTree->setItemSelected(i,false);

    ui->sceneTree->setItemSelected(newItem,true);
    //sceneNodes.insert(newNodeEnt->id(),newNode);
    sceneTreeItems.insert(newNodeEnt->id(),newItem);
    */
}

void MainWindow::deleteNode()
{
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
