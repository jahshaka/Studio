/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include <qwindow.h>
#include <qsurface.h>
#include <QScrollArea>

//#include "irisgl/src/irisgl.h"
#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/scenegraph/cameranode.h"
#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/scenegraph/lightnode.h"
#include "irisgl/src/scenegraph/viewernode.h"
#include "irisgl/src/scenegraph/particlesystemnode.h"
#include "irisgl/src/scenegraph/meshnode.h"
#include "irisgl/src/materials/defaultmaterial.h"
#include "irisgl/src/materials/custommaterial.h"
#include "irisgl/src/graphics/forwardrenderer.h"
#include "irisgl/src/graphics/mesh.h"
#include "irisgl/src/graphics/shader.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/graphics/viewport.h"
#include "irisgl/src/graphics/texture2d.h"
#include "irisgl/src/animation/keyframeset.h"
#include "irisgl/src/animation/keyframeanimation.h"
#include "irisgl/src/animation/animation.h"
#include "irisgl/src/graphics/postprocessmanager.h"
#include "irisgl/src/core/logger.h"

#include <QFontDatabase>
#include <QOpenGLContext>
#include <qstandarditemmodel.h>
#include <QKeyEvent>
#include <QMessageBox>
#include <QOpenGLDebugLogger>
#include <QUndoStack>

#include <QDockWidget>
#include <QFileDialog>

#include <QTreeWidgetItem>

#include <QPushButton>
#include <QTimer>
#include <math.h>
#include <QDesktopServices>

#include "dialogs/loadmeshdialog.h"
#include "core/surfaceview.h"
#include "core/nodekeyframeanimation.h"
#include "core/nodekeyframe.h"
#include "globals.h"

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
#include "core/materialpreset.h"
#include "widgets/postprocesseswidget.h"

#include "widgets/projectmanager.h"

#include "io/scenewriter.h"
#include "io/scenereader.h"

#include "constants.h"
#include <src/io/materialreader.hpp>
#include "uimanager.h"
#include "core/database/database.h"

#include "commands/addscenenodecommand.h"
#include "commands/deletescenenodecommand.h"

#include "widgets/screenshotwidget.h"
#include "editor/editordata.h"
#include "widgets/assetwidget.h"

#include "../src/dialogs/newprojectdialog.h"

#include "../src/widgets/sceneheirarchywidget.h"
#include "../src/widgets/scenenodepropertieswidget.h"

#include "../src/widgets/materialsets.h"
#include "../src/widgets/modelpresets.h"
#include "../src/widgets/skypresets.h"

enum class VRButtonMode : int
{
    Default = 0,
    Disabled,
    VRMode
};

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    UiManager::mainWindow = this;

    QFont font;
    font.setFamily(font.defaultFamily());
    font.setPointSize(font.pointSize() * devicePixelRatio());
    setFont(font);

#ifdef QT_DEBUG
    iris::Logger::getSingleton()->init(getAbsoluteAssetPath("jahshaka.log"));
#else
    iris::Logger::getSingleton()->init(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)+"/jahshaka.log");
#endif

    createPostProcessDockWidget();

    ui->sceneContainer->setAcceptDrops(true);
    ui->sceneContainer->installEventFilter(this);

    settings = SettingsManager::getDefaultManager();
    prefsDialog = new PreferencesDialog(settings);
    aboutDialog = new AboutDialog();
    licenseDialog = new LicenseDialog();

    camControl = nullptr;
    vrMode = false;

    setupFileMenu();
    setupHelpMenu();

    this->setupLayerButtonMenu();

    if (devicePixelRatio() > 1) {
        ui->controlBar->setFixedHeight(ui->controlBar->height() - devicePixelRatio());
    }

    sceneView = new SceneViewWidget(ui->backgroundscene);
    sceneView->setParent(ui->backgroundscene);
    sceneView->setFocusPolicy(Qt::ClickFocus);
    sceneView->setFocus();
    Globals::sceneViewWidget = sceneView;
    UiManager::setSceneViewWidget(sceneView);

    QGridLayout* layout = new QGridLayout(ui->sceneContainer);
    layout->addWidget(sceneView);
    layout->setMargin(0);
    ui->sceneContainer->setLayout(layout);

    connect(sceneView,  SIGNAL(initializeGraphics(SceneViewWidget*, QOpenGLFunctions_3_2_Core*)),
            this,       SLOT(initializeGraphics(SceneViewWidget*,   QOpenGLFunctions_3_2_Core*)));

    connect(sceneView,  SIGNAL(sceneNodeSelected(iris::SceneNodePtr)),
            this,       SLOT(sceneNodeSelected(iris::SceneNodePtr)));

    connect(ui->playSceneBtn, SIGNAL(clicked(bool)), SLOT(onPlaySceneButton()));

    setupProjectDB();

    pmContainer = new ProjectManager(db);

    sceneHeirarchyDock = new QDockWidget("Hierarchy");
    sceneHeirarchyDock->setObjectName(QStringLiteral("sceneHierarchyDock"));
    sceneHeirarchyWidget = new SceneHeirarchyWidget;
    sceneHeirarchyWidget->setMinimumWidth(396);
    sceneHeirarchyDock->setObjectName(QStringLiteral("sceneHierarchyWidget"));
    sceneHeirarchyDock->setWidget(sceneHeirarchyWidget);
    sceneHeirarchyWidget->setMainWindow(this);

    UiManager::sceneHeirarchyWidget = sceneHeirarchyWidget;

    connect(sceneHeirarchyWidget, SIGNAL(sceneNodeSelected(iris::SceneNodePtr)),
            this,           SLOT(sceneNodeSelected(iris::SceneNodePtr)));


    // Since this widget can be longer than there is screen space, we need to add a QScrollArea
    // For this to also work, we need a "holder widget" that will have a layout and the scroll area
    sceneNodePropertiesDock = new QDockWidget("Properties");
    sceneNodePropertiesDock->setObjectName(QStringLiteral("sceneNodePropertiesDock"));
    sceneNodePropertiesWidget = new SceneNodePropertiesWidget;
    sceneNodePropertiesWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
    sceneNodePropertiesWidget->setObjectName(QStringLiteral("sceneNodePropertiesWidget"));

    auto sceneNodeDockWidgetContents = new QWidget();

    auto sceneNodeScrollArea = new QScrollArea(sceneNodeDockWidgetContents);
    sceneNodeScrollArea->setMinimumWidth(396);
    sceneNodeScrollArea->setStyleSheet("border: 0");
    sceneNodeScrollArea->setSizeAdjustPolicy(QAbstractScrollArea::AdjustToContents);
    sceneNodeScrollArea->setWidget(sceneNodePropertiesWidget);
    sceneNodeScrollArea->setWidgetResizable(true);
    sceneNodeScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto sceneNodeLayout = new QVBoxLayout(sceneNodeDockWidgetContents);
    sceneNodeLayout->setContentsMargins(0, 0, 0, 0);
    sceneNodeLayout->addWidget(sceneNodeScrollArea);

    sceneNodeDockWidgetContents->setLayout(sceneNodeLayout);

    sceneNodePropertiesDock->setWidget(sceneNodeDockWidgetContents);

    // presets
    presetsDock = new QDockWidget("Presets");
    presetsDock->setObjectName(QStringLiteral("presetsDock"));

    auto presetDockContents = new QWidget;

    auto materialPresets = new MaterialSets;
    materialPresets->setMainWindow(this);
    auto skyPresets = new SkyPresets;
    skyPresets->setMainWindow(this);
    auto modelPresets = new ModelPresets;
    modelPresets->setMainWindow(this);

    presetsTabWidget = new QTabWidget;
    presetsTabWidget->setMinimumWidth(396);
    presetsTabWidget->addTab(materialPresets, "Materials");
    presetsTabWidget->addTab(skyPresets, "Skyboxes");
    presetsTabWidget->addTab(modelPresets, "Primitives");
    presetDockContents->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);

    auto presetsLayout = new QGridLayout(presetDockContents);
    presetsLayout->setContentsMargins(0, 0, 0, 0);
    presetsLayout->addWidget(presetsTabWidget);

    presetsDock->setWidget(presetDockContents);

    assetDock = new QDockWidget("Asset Browser");
    assetDock->setObjectName(QStringLiteral("assetDock"));

    assetWidget = new AssetWidget;
    assetWidget->setAcceptDrops(true);
    assetWidget->installEventFilter(this);

    auto assetDockContents = new QWidget;

    auto assetsLayout = new QGridLayout(assetDockContents);
    assetsLayout->addWidget(assetWidget);
    assetsLayout->setContentsMargins(0, 0, 0, 0);
    assetDock->setWidget(assetDockContents);

    animationDock = new QDockWidget("Timeline");
    animationDock->setObjectName(QStringLiteral("animationDock"));
    animationWidget = new AnimationWidget;
    UiManager::setAnimationWidget(animationWidget);

    auto animationDockContents = new QWidget;
    auto animationLayout = new QGridLayout(animationDockContents);
    animationLayout->setContentsMargins(0, 0, 0, 0);
    animationLayout->addWidget(animationWidget);

    animationDock->setWidget(animationDockContents);

    timer = new QTimer(this);
    connect(timer, SIGNAL(timeout()), this, SLOT(updateAnim()));

    addDockWidget(Qt::LeftDockWidgetArea, sceneHeirarchyDock);
    addDockWidget(Qt::RightDockWidgetArea, sceneNodePropertiesDock);
    addDockWidget(Qt::BottomDockWidgetArea, assetDock);
    addDockWidget(Qt::BottomDockWidgetArea, animationDock);
    addDockWidget(Qt::BottomDockWidgetArea, presetsDock);
    tabifyDockWidget(animationDock, assetDock);

    connect(pmContainer, SIGNAL(fileToOpen(QString)), SLOT(openProject(QString)));
    connect(pmContainer, SIGNAL(fileToPlay(QString)), SLOT(playProject(QString)));
    connect(pmContainer, SIGNAL(fileToCreate(QString, QString)), SLOT(newProject(QString, QString)));

    // toolbar stuff
    connect(ui->actionTranslate,    SIGNAL(triggered(bool)), SLOT(translateGizmo()));
    connect(ui->actionRotate,       SIGNAL(triggered(bool)), SLOT(rotateGizmo()));
    connect(ui->actionScale,        SIGNAL(triggered(bool)), SLOT(scaleGizmo()));

    transformGroup = new QActionGroup(this);
    transformGroup->addAction(ui->actionTranslate);
    transformGroup->addAction(ui->actionRotate);
    transformGroup->addAction(ui->actionScale);

    connect(ui->actionGlobalSpace,  SIGNAL(triggered(bool)), SLOT(useGlobalTransform()));
    connect(ui->actionLocalSpace,   SIGNAL(triggered(bool)), SLOT(useLocalTransform()));

    transformSpaceGroup = new QActionGroup(this);
    transformSpaceGroup->addAction(ui->actionGlobalSpace);
    transformSpaceGroup->addAction(ui->actionLocalSpace);
    ui->actionGlobalSpace->setChecked(true);

    connect(ui->actionFreeCamera,   SIGNAL(triggered(bool)), SLOT(useFreeCamera()));
    connect(ui->actionArcballCam,   SIGNAL(triggered(bool)), SLOT(useArcballCam()));

    cameraGroup = new QActionGroup(this);
    cameraGroup->addAction(ui->actionFreeCamera);
    cameraGroup->addAction(ui->actionArcballCam);
    ui->actionFreeCamera->setChecked(true);

    connect(ui->screenshotBtn, SIGNAL(pressed()), this, SLOT(takeScreenshot()));

    ui->wireCheck->setChecked(sceneView->getShowLightWires());
    connect(ui->wireCheck, SIGNAL(toggled(bool)), this, SLOT(toggleLightWires(bool)));

    // this acts as a spacer
    QWidget* empty = new QWidget();
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->ToolBar->addWidget(empty);

    pmButton = new QPushButton();
    QIcon ico(":/icons/home.svg");
    pmButton->setIcon(ico);
    pmButton->setObjectName("pmButton");

    connect(pmButton, SIGNAL(clicked(bool)), SLOT(showProjectManagerInternal(bool)));

    vrButton = new QPushButton();
    QIcon icovr(":/icons/virtual-reality.svg");
    vrButton->setIcon(icovr);
    vrButton->setObjectName("vrButton");
    //but->setStyleSheet("background-color: #1e1e1e; padding: 8px; border: 1px solid black; margin: 8px;");
    ui->ToolBar->addWidget(pmButton);
    ui->ToolBar->addWidget(vrButton);

//    if (!UiManager::playMode) {
//        restoreGeometry(settings->getValue("geometry", "").toByteArray());
//        restoreState(settings->getValue("windowState", "").toByteArray());
//    }

    setupUndoRedo();

    // this ties to hidden geometry so should come at the end
    setupViewMenu();

#ifdef QT_DEBUG
    setWindowTitle("Jahshaka 0.3a - Developer Build");
#endif
}

void MainWindow::initialize()
{

}

void MainWindow::setupVrUi()
{
    vrButton->setToolTipDuration(0);

    if (sceneView->isVrSupported()) {
        vrButton->setEnabled(true);
        vrButton->setToolTip("Press to view the scene in vr");
        vrButton->setProperty("vrMode", (int) VRButtonMode::Default);
    } else {
        vrButton->setEnabled(false);
        vrButton->setToolTip("No Oculus device detected");
        vrButton->setProperty("vrMode", (int) VRButtonMode::Disabled);
    }

    connect(vrButton, SIGNAL(clicked(bool)), SLOT(vrButtonClicked(bool)));

    // needed to apply changes
    vrButton->style()->unpolish(vrButton);
    vrButton->style()->polish(vrButton);
}

/**
 * uses style property trick
 * http://wiki.qt.io/Dynamic_Properties_and_Stylesheets
 */
void MainWindow::vrButtonClicked(bool)
{
    if (!sceneView->isVrSupported()) {
        // pass
    } else {
        if (sceneView->getViewportMode()==ViewportMode::Editor) {
            sceneView->setViewportMode(ViewportMode::VR);

            // highlight button blue
            vrButton->setProperty("vrMode",(int)VRButtonMode::VRMode);
        } else {
            sceneView->setViewportMode(ViewportMode::Editor);

            // return button back to normal color
            vrButton->setProperty("vrMode",(int)VRButtonMode::Default);
        }
    }

    // needed to apply changes
    vrButton->style()->unpolish(vrButton);
    vrButton->style()->polish(vrButton);
}

iris::ScenePtr MainWindow::getScene()
{
    return scene;
}

QString MainWindow::getAbsoluteAssetPath(QString pathRelativeToApp)
{
    QDir basePath = QDir(QCoreApplication::applicationDirPath());

#if defined(WIN32) && defined(QT_DEBUG)
    basePath.cdUp();
#elif defined(Q_OS_MAC)
    basePath.cdUp();
    basePath.cdUp();
    basePath.cdUp();
#endif

    auto path = QDir::cleanPath(basePath.absolutePath() + QDir::separator() + pathRelativeToApp);
    return path;
}

iris::ScenePtr MainWindow::createDefaultScene()
{
    auto scene = iris::Scene::create();

    scene->setSkyColor(QColor(72, 72, 72));
    scene->setAmbientColor(QColor(96, 96, 96));

    // second node
    auto node = iris::MeshNode::create();
    node->setMesh(":/models/ground.obj");
    node->setLocalPos(QVector3D(0, 1e-4, 0)); // prevent z-fighting with the default plane (iKlsR)
    node->setName("Ground");
    node->setPickable(false);
    node->setShadowEnabled(false);

    auto m = iris::CustomMaterial::create();
    m->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
    m->setValue("diffuseTexture", ":/content/textures/tile.png");
    m->setValue("textureScale", 4.f);
    node->setMaterial(m);

    scene->rootNode->addChild(node);

    auto dlight = iris::LightNode::create();
    dlight->setLightType(iris::LightType::Directional);
    scene->rootNode->addChild(dlight);
    dlight->setName("Directional Light");
    dlight->setLocalPos(QVector3D(4, 4, 0));
    dlight->setLocalRot(QQuaternion::fromEulerAngles(15, 0, 0));
    dlight->intensity = 1;
    dlight->icon = iris::Texture2D::load(":/icons/light.png");

    auto plight = iris::LightNode::create();
    plight->setLightType(iris::LightType::Point);
    scene->rootNode->addChild(plight);
    plight->setName("Point Light");
    plight->setLocalPos(QVector3D(-4, 4, 0));
    plight->intensity = 1;
    plight->icon = iris::Texture2D::load(":/icons/bulb.png");

    // fog params
    scene->fogColor = QColor(72, 72, 72);
    scene->shadowEnabled = true;

    sceneNodeSelected(scene->rootNode);

    return scene;
}

void MainWindow::initializeGraphics(SceneViewWidget *widget, QOpenGLFunctions_3_2_Core *gl)
{
    Q_UNUSED(gl);
    postProcessWidget->setPostProcessMgr(widget->getRenderer()->getPostProcessManager());
    setupVrUi();
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

    return true;
}

bool MainWindow::handleMouseRelease(QMouseEvent *event)
{
    return true;
}

bool MainWindow::handleMouseMove(QMouseEvent *event)
{
    mousePos = event->pos();
    return false;
}

// TODO - disable scrolling while doing gizmo transform ?
bool MainWindow::handleMouseWheel(QWheelEvent *event)
{
    return false;
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event)
{
    switch (event->type()) {
        case QEvent::DragMove: {
            auto evt = static_cast<QDragMoveEvent*>(event);
            if (obj == ui->sceneContainer) {
                auto info = QFileInfo(evt->mimeData()->text());

                if (!!activeSceneNode) {
                    sceneView->hideGizmo();

                    if (isModelExtension(info.suffix())) {
                        if (sceneView->doActiveObjectPicking(evt->posF())) {
                            //activeSceneNode->pos = sceneView->hit;
                            dragScenePos = sceneView->hit;
                        } else if (sceneView->updateRPI(sceneView->editorCam->getLocalPos(),
                                                        sceneView->calculateMouseRay(evt->posF())))
                        {
                            //activeSceneNode->pos = sceneView->Offset;
                            dragScenePos = sceneView->Offset;
                        } else {
                            ////////////////////////////////////////
                            const float spawnDist = 10.0f;
                            auto offset = sceneView->editorCam->getLocalRot().rotatedVector(QVector3D(0, -1.0f, -spawnDist));
                            offset += sceneView->editorCam->getLocalPos();
                            //activeSceneNode->pos = offset;
                            dragScenePos = offset;
                        }
                    }
                }

                if (!isModelExtension(info.suffix())) {
                    sceneView->doObjectPicking(evt->posF(), iris::SceneNodePtr(), false, true);
                }
            }
        }

        case QEvent::DragEnter: {
            auto evt = static_cast<QDragEnterEvent*>(event);

            sceneView->hideGizmo();

            if (obj == ui->sceneContainer) {
                if (evt->mimeData()->hasText()) {
                    evt->acceptProposedAction();
                } else {
                    evt->ignore();
                }

                auto info = QFileInfo(evt->mimeData()->text());
                if (isModelExtension(info.suffix())) {

                    if (dragging) {
                        // TODO swap this with the actual model later on
                        addDragPlaceholder();
                        dragging = !dragging;
                    }
                }
            }

            break;
        }

        case QEvent::Drop: {
            if (obj == ui->sceneContainer) {
                auto evt = static_cast<QDropEvent*>(event);

                auto info = QFileInfo(evt->mimeData()->text());
                if (evt->mimeData()->hasText()) {
                    evt->acceptProposedAction();
                } else {
                    evt->ignore();
                }

                if (isModelExtension(info.suffix())) {
                    //auto ppos = activeSceneNode->pos;
                    auto ppos = dragScenePos;
                    //deleteNode();
                    addMesh(evt->mimeData()->text(), true, ppos);
                }

                if (!!activeSceneNode && !isModelExtension(info.suffix())) {
                    auto meshNode = activeSceneNode.staticCast<iris::MeshNode>();
                    auto mat = meshNode->getMaterial().staticCast<iris::CustomMaterial>();

                    if (!mat->firstTextureSlot().isEmpty()) {
                        mat->setValue(mat->firstTextureSlot(), evt->mimeData()->text());
                    }
                }
            }

            break;
        }

        case QEvent::MouseButtonPress: {
            dragging = true;

            if (obj == surface) return handleMousePress(static_cast<QMouseEvent*>(event));

            if (obj == ui->sceneContainer) {
                sceneView->mousePressEvent(static_cast<QMouseEvent*>(event));
            }

            break;
        }

        case QEvent::MouseButtonRelease: {
            if (obj == surface) return handleMouseRelease(static_cast<QMouseEvent*>(event));
            break;
        }

        case QEvent::MouseMove: {
            if (obj == surface) return handleMouseMove(static_cast<QMouseEvent*>(event));
            break;
        }

        case QEvent::Wheel: {
            if (obj == surface) return handleMouseWheel(static_cast<QWheelEvent*>(event));
            break;
        }

        default:
            break;
    }

    return false;
}

void MainWindow::closeEvent(QCloseEvent *event)
{
//    event->accept();
    event->ignore();

    if (UiManager::isUndoStackDirty()) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this,
                                      "Unsaved Changes",
                                      "There are unsaved changes, save before closing?",
                                      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (reply == QMessageBox::Yes) {
            saveScene();
            event->accept();
            this->close();
        } else if (reply == QMessageBox::No) {
            event->accept();
            this->close();
        }
    } else {
        event->accept();
    }

//    if (!UiManager::playMode) {
//        settings->setValue("geometry", saveGeometry());
//        settings->setValue("windowState", saveState());
//    }

    ThumbnailGenerator::getSingleton()->shutdown();
}

void MainWindow::setupFileMenu()
{
    connect(ui->actionSave,             SIGNAL(triggered(bool)), this, SLOT(saveScene()));
    connect(ui->actionExit,             SIGNAL(triggered(bool)), this, SLOT(exitApp()));
    connect(ui->actionPreferences,      SIGNAL(triggered(bool)), this, SLOT(showPreferences()));
    connect(ui->actionNewProject,       SIGNAL(triggered(bool)), this, SLOT(newSceneProject()));
    connect(ui->actionManage_Projects,  SIGNAL(triggered(bool)), this, SLOT(callProjectManager()));

//    connect(ui->actionOpen,     SIGNAL(triggered(bool)), this, SLOT(newSceneProject()));
    connect(ui->actionClose,    SIGNAL(triggered(bool)), this, SLOT(showProjectManagerInternal(bool)));
//    connect(ui->actionDelete,   SIGNAL(triggered(bool)), this, SLOT(deleteProject()));

    connect(prefsDialog,  SIGNAL(PreferencesDialogClosed()), this, SLOT(updateSceneSettings()));
}

void MainWindow::setupViewMenu()
{
    connect(ui->actionOutliner, &QAction::toggled, [this](bool set) {
        sceneHeirarchyDock->setVisible(set);
    });

    connect(ui->actionProperties, &QAction::toggled, [this](bool set) {
        sceneNodePropertiesDock->setVisible(set);
    });

    connect(ui->actionPresets, &QAction::toggled, [this](bool set) {
        presetsDock->setVisible(set);
    });

    connect(ui->actionAnimation, &QAction::toggled, [this](bool set) {
        animationDock->setVisible(set);
    });

    connect(ui->actionAssets, &QAction::toggled, [this](bool set) {
        assetDock->setVisible(set);
    });

    connect(ui->actionClose_All, &QAction::triggered, [this]() {
        toggleWidgets(false);
    });

    connect(ui->actionRestore_All, &QAction::triggered, [this]() {
        toggleWidgets(true);
    });
}

void MainWindow::setupHelpMenu()
{
    connect(ui->actionLicense,      SIGNAL(triggered(bool)), this, SLOT(showLicenseDialog()));
    connect(ui->actionAbout,        SIGNAL(triggered(bool)), this, SLOT(showAboutDialog()));
    connect(ui->actionFacebook,     SIGNAL(triggered(bool)), this, SLOT(openFacebookUrl()));
    connect(ui->actionOpenWebsite,  SIGNAL(triggered(bool)), this, SLOT(openWebsiteUrl()));
}

void MainWindow::createPostProcessDockWidget()
{
    postProcessDockWidget = new QDockWidget(this);
    postProcessWidget = new PostProcessesWidget();
    // postProcessWidget->setWindowTitle("Post Processes");
    postProcessDockWidget->setWidget(postProcessWidget);
    postProcessDockWidget->setWindowTitle("PostProcesses");
    // postProcessDockWidget->setFloating(true);
    postProcessDockWidget->setHidden(true);
    // this->addDockWidget(Qt::RightDockWidgetArea, postProcessDockWidget);

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
    this->sceneHeirarchyWidget->repopulateTree();
}

void MainWindow::updateGizmoTransform()
{
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);

    //float aspectRatio = ui->sceneContainer->width()/(float)ui->sceneContainer->height();
    //editorCam->lens()->setPerspectiveProjection(45.0f, aspectRatio, 0.1f, 1000.0f);
}

void MainWindow::stopAnimWidget()
{
    animWidget->stopAnimation();
}

void MainWindow::setupProjectDB()
{
    db = new Database();

    // new
    db->initializeDatabase("C:/Users/iKlsR/Desktop/ProjectDatabase.db");
    db->createGlobalDb();
}

void MainWindow::setupUndoRedo()
{
    undoStack = new QUndoStack(this);
    UiManager::setUndoStack(undoStack);
    UiManager::mainWindow = this;

    connect(ui->actionUndo, &QAction::triggered, [this]() {
        undo();
        UiManager::updateWindowTitle();
    });

    connect(ui->actionEditUndo, &QAction::triggered, [this]() {
        undo();
        UiManager::updateWindowTitle();
    });

    ui->actionEditUndo->setShortcuts(QKeySequence::Undo);

    connect(ui->actionRedo, &QAction::triggered, [this]() {
        redo();
        UiManager::updateWindowTitle();
    });

    connect(ui->actionEditRedo, &QAction::triggered, [this]() {
        redo();
        UiManager::updateWindowTitle();
    });

    ui->actionEditRedo->setShortcuts(QKeySequence::Redo);
}

void MainWindow::saveScene()
{
    auto writer = new SceneWriter();
    auto blob = writer->getSceneObject(Globals::project->getFilePath(),
                                       scene,
                                       sceneView->getRenderer()->getPostProcessManager(),
                                       sceneView->getEditorData());
    db->updateScene(blob);

    auto img = sceneView->takeScreenshot(Constants::TILE_SIZE.width(), Constants::TILE_SIZE.height());
    img.save(Globals::project->getProjectFolder() + "/Metadata/preview.png");
}

void MainWindow::saveSceneAs()
{
}

void MainWindow::loadScene()
{
    QString dir = QApplication::applicationDirPath() + "/scenes/";
    auto filename = QFileDialog::getOpenFileName(this, "Open Scene File", dir, "Jashaka Scene (*.jah)");

    if (filename.isEmpty() || filename.isNull()) return;

//    openProject(filename);
}

void MainWindow::openProject(QString filename)
{
    if (!this->isVisible()) {
        this->showMaximized();
    }

    this->sceneView->makeCurrent();
    //remove current scene first
    this->removeScene();

    //load new scene
    auto reader = new SceneReader();

    EditorData* editorData = nullptr;

    // db->initializeDatabase(filename);

    Globals::project->setFilePath(filename);
    UiManager::updateWindowTitle();

    auto postMan = sceneView->getRenderer()->getPostProcessManager();
    postMan->clearPostProcesses();
    auto scene = reader->readScene(filename, db->getSceneBlobGlobal(), postMan, &editorData);

    toggleWidgets(true);
    setScene(scene);

    // use new post process that has fxaa by default
    // @todo: remember to find a better replacement
    postProcessWidget->setPostProcessMgr(iris::PostProcessManager::create());
    this->sceneView->doneCurrent();

    if (editorData != nullptr) {
        sceneView->setEditorData(editorData);
        ui->wireCheck->setChecked(editorData->showLightWires);
    }

    assetWidget->trigger();

    delete reader;
}

void MainWindow::playProject(QString filename)
{
    if (!this->isVisible()) {
        this->showMaximized();
    }

    this->sceneView->makeCurrent();
    //remove current scene first
    this->removeScene();

    //load new scene
    auto reader = new SceneReader();

    EditorData* editorData = nullptr;

    db->initializeDatabase(filename);

    Globals::project->setFilePath(filename);
    UiManager::updateWindowTitle();

    auto postMan = sceneView->getRenderer()->getPostProcessManager();
    postMan->clearPostProcesses();
    auto scene = reader->readScene(filename, db->getSceneBlob(), postMan, &editorData);

    UiManager::playMode = true;
    toggleWidgets(false);

    setScene(scene);

    // use new post process that has fxaa by default
    // @todo: remember to find a better replacement
    postProcessWidget->setPostProcessMgr(iris::PostProcessManager::create());
    this->sceneView->doneCurrent();

    if (editorData != nullptr) {
        sceneView->setEditorData(editorData);
        ui->wireCheck->setChecked(editorData->showLightWires);
    }

    assetWidget->trigger();

    onPlaySceneButton();

    delete reader;
}

/// TODO - this needs to be fixed after the objects are added back to the uniforms array/obj
void MainWindow::applyMaterialPreset(MaterialPreset *preset)
{
    if (!activeSceneNode || activeSceneNode->sceneNodeType!=iris::SceneNodeType::Mesh) return;

    auto meshNode = activeSceneNode.staticCast<iris::MeshNode>();

    // TODO - set the TYPE for a preset in the .material file so we can have other preset types
    // only works for the default material at the moment...
    auto m = iris::CustomMaterial::create();
    m->generate(getAbsoluteAssetPath(Constants::DEFAULT_SHADER));

    m->setValue("diffuseTexture", preset->diffuseTexture);
    m->setValue("specularTexture", preset->specularTexture);
    m->setValue("normalTexture", preset->normalTexture);
    m->setValue("reflectionTexture", preset->reflectionTexture);

    m->setValue("ambientColor", preset->ambientColor);
    m->setValue("diffuseColor", preset->diffuseColor);
    m->setValue("specularColor", preset->specularColor);

    m->setValue("shininess", preset->shininess);
    m->setValue("normalIntensity", preset->normalIntensity);
    m->setValue("reflectionInfluence", preset->reflectionInfluence);
    m->setValue("textureScale", preset->textureScale);

    meshNode->setMaterial(m);

    // TODO: update node's material without updating the whole ui
    this->sceneNodePropertiesWidget->refreshMaterial(preset->type);
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

//    this->openProject(filename);
}

void MainWindow::setScene(QSharedPointer<iris::Scene> scene)
{
    this->scene = scene;
    this->sceneView->setScene(scene);
    this->sceneHeirarchyWidget->setScene(scene);

    // interim...
    updateSceneSettings();
}

void MainWindow::removeScene()
{
}

void MainWindow::setupPropertyUi()
{
    animWidget = new AnimationWidget();
}

void MainWindow::sceneNodeSelected(QTreeWidgetItem* item)
{

}

void MainWindow::sceneTreeItemChanged(QTreeWidgetItem* item,int column)
{

}

void MainWindow::sceneNodeSelected(iris::SceneNodePtr sceneNode)
{
    //show properties for scenenode
    activeSceneNode = sceneNode;

    sceneView->setSelectedNode(sceneNode);
    this->sceneNodePropertiesWidget->setSceneNode(sceneNode);
    this->sceneHeirarchyWidget->setSelectedNode(sceneNode);
    animationWidget->setSceneNode(sceneNode);
}


void MainWindow::setupLayerButtonMenu()
{
}

void MainWindow::updateAnim()
{
}

void MainWindow::setSceneAnimTime(float time)
{
}

void MainWindow::addPlane()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/plane.obj");
    node->setFaceCullingMode(iris::FaceCullingMode::None);
    node->setName("Plane");
    addNodeToScene(node);
}

void MainWindow::addGround()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/models/ground.obj");
    node->setFaceCullingMode(iris::FaceCullingMode::None);
    node->setName("Ground");
    addNodeToScene(node);
}


void MainWindow::addCone()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/cone.obj");
    node->setName("Cone");
    addNodeToScene(node);
}

/**
 * Adds a cube to the current scene
 */
void MainWindow::addCube()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/cube.obj");
    node->setName("Cube");
    addNodeToScene(node);
}

/**
 * Adds a torus to the current scene
 */
void MainWindow::addTorus()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/torus.obj");
    node->setName("Torus");
    addNodeToScene(node);
}

/**
 * Adds a sphere to the current scene
 */
void MainWindow::addSphere()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/sphere.obj");
    node->setName("Sphere");
    addNodeToScene(node);
}

/**
 * Adds a cylinder to the current scene
 */
void MainWindow::addCylinder()
{
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->setMesh(":/content/primitives/cylinder.obj");
    node->setName("Cylinder");

    addNodeToScene(node);
}

void MainWindow::addPointLight()
{
    this->sceneView->makeCurrent();
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Point);
    node->icon = iris::Texture2D::load(":/icons/bulb.png");
    node->setName("Point Light");
    node->intensity = 1.0f;
    node->distance = 40.0f;
    addNodeToScene(node);
}

void MainWindow::addSpotLight()
{
    this->sceneView->makeCurrent();
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Spot);
    node->icon = iris::Texture2D::load(":/icons/bulb.png");
    node->setName("Spot Light");
    addNodeToScene(node);
}


void MainWindow::addDirectionalLight()
{
    this->sceneView->makeCurrent();
    auto node = iris::LightNode::create();
    node->setLightType(iris::LightType::Directional);
    node->icon = iris::Texture2D::load(":/icons/bulb.png");
    node->setName("Directional Light");
    addNodeToScene(node);
}

void MainWindow::addEmpty()
{
    this->sceneView->makeCurrent();
    auto node = iris::SceneNode::create();
    node->setName("Empty");
    addNodeToScene(node);
}

void MainWindow::addViewer()
{
    this->sceneView->makeCurrent();
    auto node = iris::ViewerNode::create();
    node->setName("Viewer");
    addNodeToScene(node);
}

void MainWindow::addParticleSystem()
{
    this->sceneView->makeCurrent();
    auto node = iris::ParticleSystemNode::create();
    node->setName("Particle System");
    addNodeToScene(node);
}

void MainWindow::addMesh(const QString &path, bool ignore, QVector3D position)
{
    QString filename;
    if (path.isEmpty()) {
        filename = QFileDialog::getOpenFileName(this, "Load Mesh", "Mesh Files (*.obj *.fbx *.3ds)");
    } else {
        filename = path;
    }

    auto nodeName = QFileInfo(filename).baseName();
    if (filename.isEmpty()) return;

    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::loadAsSceneFragment(filename,[](iris::MeshPtr mesh, iris::MeshMaterialData& data)
    {
        auto mat = iris::CustomMaterial::create();
        //MaterialReader *materialReader = new MaterialReader();
        //materialReader->readJahShader(IrisUtils::getAbsoluteAssetPath("app/shader_defs/Default.shader"));
        //mat->generate(materialReader->getParsedShader());
        if (mesh->hasSkeleton())
            mat->generate(IrisUtils::getAbsoluteAssetPath("app/shader_defs/DefaultAnimated.shader"));
        else
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
    auto relPath = QDir(Globals::project->folderPath).relativeFilePath(filename);
    for(auto anim : node->getAnimations()) {
        if (!!anim->skeletalAnimation)
            anim->skeletalAnimation->source = relPath;
    }

//    node->materialType = 2;
    //node->setName(nodeName);
    node->setLocalPos(position);

    // todo: load material data
    addNodeToScene(node, ignore);
}

void MainWindow::addViewPoint()
{

}

void MainWindow::addDragPlaceholder()
{
    /*
    this->sceneView->makeCurrent();
    auto node = iris::MeshNode::create();
    node->scale = QVector3D(.5f, .5f, .5f);
    node->setMesh(":app/content/primitives/arrow.obj");
    node->setName("Arrow");
    addNodeToScene(node, true);
    */
}

void MainWindow::addTexturedPlane()
{

}

/**
 * Adds sceneNode to selected scene node. If there is no selected scene node,
 * sceneNode is added to the root node
 * @param sceneNode
 */
void MainWindow::addNodeToActiveNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    if (!scene) {
        //todo: set alert that a scene needs to be set before this can be done
    }

    // apply default material
    if (sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();

        if (!meshNode->getMaterial()) {
            auto mat = iris::DefaultMaterial::create();
            meshNode->setMaterial(mat);
        }
    }

    if (!!activeSceneNode) {
        activeSceneNode->addChild(sceneNode);
    } else {
        scene->getRootNode()->addChild(sceneNode);
    }

    this->sceneHeirarchyWidget->repopulateTree();
}

/**
 * adds sceneNode directly to the scene's rootNode
 * applied default material to mesh if one isnt present
 * ignore set to false means we only add it visually, usually to discard it afterw
 */
void MainWindow::addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, bool ignore)
{
    if (!scene) {
        // @TODO: set alert that a scene needs to be set before this can be done
        return;
    }

    // @TODO: add this to a constants file
    if (!ignore) {
        const float spawnDist = 10.0f;
        auto offset = sceneView->editorCam->getLocalRot().rotatedVector(QVector3D(0, -1.0f, -spawnDist));
        offset += sceneView->editorCam->getLocalPos();
        sceneNode->setLocalPos(offset);
    }

    // apply default material to mesh nodes
    if (sceneNode->sceneNodeType == iris::SceneNodeType::Mesh) {
        auto meshNode = sceneNode.staticCast<iris::MeshNode>();
        if (!meshNode->getMaterial()) {
            auto mat = iris::CustomMaterial::create();
            mat->generate(IrisUtils::getAbsoluteAssetPath(Constants::DEFAULT_SHADER));
            meshNode->setMaterial(mat);
        }
    }

    /*
    scene->getRootNode()->addChild(sceneNode, false);
    ui->sceneHierarchy->repopulateTree();
    sceneNodeSelected(sceneNode);
    */
    auto cmd = new AddSceneNodeCommand(scene->getRootNode(), sceneNode);
    UiManager::pushUndoStack(cmd);
}

void MainWindow::repopulateSceneTree()
{
    this->sceneHeirarchyWidget->repopulateTree();
}

void MainWindow::duplicateNode()
{
    if (!scene) return;
    if (!activeSceneNode || !activeSceneNode->isDuplicable()) return;

    auto node = activeSceneNode->duplicate();
    activeSceneNode->parent->addChild(node, false);

    this->sceneHeirarchyWidget->repopulateTree();
    sceneNodeSelected(node);
}

void MainWindow::deleteNode()
{
    if (!!activeSceneNode) {
        /*
        activeSceneNode->removeFromParent();
        ui->sceneHierarchy->repopulateTree();
        sceneView->clearSelectedNode();
        ui->sceneNodeProperties->setSceneNode(QSharedPointer<iris::SceneNode>(nullptr));
        sceneView->hideGizmo();
        */
        auto cmd = new DeleteSceneNodeCommand(activeSceneNode->parent, activeSceneNode);
        UiManager::pushUndoStack(cmd);
    }
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

}

void MainWindow::dragLeaveEvent(QDragLeaveEvent *event)
{
    event->accept();
}

bool MainWindow::isModelExtension(QString extension)
{
    if(extension == "obj" ||
       extension == "3ds" ||
       extension == "fbx" ||
       extension == "dae")
        return true;
    return false;
}

QIcon MainWindow::getIconFromSceneNodeType(SceneNodeType type)
{
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

void MainWindow::undo()
{
    if (undoStack->canUndo())
        undoStack->undo();
}

void MainWindow::redo()
{
    if (undoStack->canRedo())
        undoStack->redo();
}

void MainWindow::takeScreenshot()
{
    auto img = sceneView->takeScreenshot();
    ScreenshotWidget screenshotWidget;
    screenshotWidget.setMaximumWidth(1280);
    screenshotWidget.setMaximumHeight(720);
    screenshotWidget.layout()->setSizeConstraint(QLayout::SetNoConstraint);
    screenshotWidget.setImage(img);
    screenshotWidget.exec();
}

void MainWindow::toggleLightWires(bool state)
{
    sceneView->setShowLightWires(state);
}

void MainWindow::toggleWidgets(bool state)
{
    ui->ToolBar->setVisible(state);
    sceneHeirarchyDock->setVisible(state);
    sceneNodePropertiesDock->setVisible(state);
    presetsDock->setVisible(state);
    assetDock->setVisible(state);
    animationDock->setVisible(state);
}

void MainWindow::tabsChanged(int index)
{
    if (index == 1) {
//        pm->test();
    }
}

void MainWindow::showProjectManager()
{
    pmContainer->showMaximized();
}

void MainWindow::showProjectManagerInternal(bool running)
{
    Q_UNUSED(running);
    saveScene();
    hide();
    pmContainer->showMaximized();
    pmContainer->updateAfter();
}

void MainWindow::newScene()
{
    this->showMaximized();

    this->sceneView->makeCurrent();
    auto scene = this->createDefaultScene();
    this->setScene(scene);
    this->sceneView->resetEditorCam();
    this->sceneView->doneCurrent();
}

void MainWindow::newSceneProject()
{
    if (UiManager::isUndoStackDirty()) {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this,
                                      "Unsaved Changes",
                                      "There are unsaved changes, save first?",
                                      QMessageBox::Yes | QMessageBox::No | QMessageBox::Cancel);
        if (reply == QMessageBox::Yes) {
            saveScene();
            this->db->closeDb();
            delete ui;
            this->close();
            projectManager();
        } else if (reply == QMessageBox::No) {
            this->db->closeDb();
            delete ui;
            this->close();
            projectManager();
        }
    } else {
        this->db->closeDb();
        delete ui;
        this->close();
        projectManager();
    }
}

void MainWindow::deleteProject()
{
    QMessageBox::StandardButton reply;
    reply = QMessageBox::question(this,
                                  "Deleting Project",
                                  "Are you sure you want to delete this project?",
                                  QMessageBox::Yes | QMessageBox::Cancel);
    if (reply == QMessageBox::Yes) {
        this->db->closeDb();
        delete ui;
        this->close();

        QDir dir(Globals::project->folderPath);
        if (dir.removeRecursively()) {
            settings->removeRecentlyOpenedEntry(Globals::project->filePath);
        } else {
            QMessageBox::StandardButton err;
            err = QMessageBox::warning(this,
                                       "Delete failed",
                                       "Failed to remove project, please delete manually",
                                       QMessageBox::Ok);
        }

        projectManager();
    }
}

void MainWindow::callProjectManager()
{
    projectManager(true);
}

void MainWindow::projectManager(bool mainWindowActive)
{

}

void MainWindow::newProject(const QString &filename, const QString &projectPath)
{
    newScene();

    auto pPath = QDir(projectPath).filePath(filename + Constants::PROJ_EXT);

//    db->initializeDatabase(pPath);
//    db->createProject(Constants::DB_ROOT_TABLE);

    auto writer = new SceneWriter();
    auto sceneObject = writer->getSceneObject(pPath,
                                              this->scene,
                                              sceneView->getRenderer()->getPostProcessManager(),
                                              sceneView->getEditorData());
    db->insertSceneGlobal(filename, sceneObject);

    UiManager::updateWindowTitle();

    assetWidget->trigger();

    delete writer;
}

void MainWindow::showAboutDialog()
{
    aboutDialog->exec();
}

void MainWindow::showLicenseDialog()
{
    licenseDialog->exec();
}

void MainWindow::openFacebookUrl()
{
    QDesktopServices::openUrl(QUrl("https://www.facebook.com/jahshakafx/"));
}

void MainWindow::openWebsiteUrl()
{
    QDesktopServices::openUrl(QUrl("http://www.jahshaka.com/"));
}

MainWindow::~MainWindow()
{
    this->db->closeDb();
    delete ui;
}

void MainWindow::useFreeCamera()
{
    sceneView->setFreeCameraMode();
}

void MainWindow::useArcballCam()
{
    sceneView->setArcBallCameraMode();
}

void MainWindow::useLocalTransform()
{
    sceneView->setTransformOrientationLocal();
}

void MainWindow::useGlobalTransform()
{
    sceneView->setTransformOrientationGlobal();
}

void MainWindow::translateGizmo()
{
    sceneView->setGizmoLoc();
}

void MainWindow::rotateGizmo()
{
    sceneView->setGizmoRot();
}

void MainWindow::scaleGizmo()
{
    sceneView->setGizmoScale();
}

void MainWindow::onPlaySceneButton()
{
    UiManager::isScenePlaying = !UiManager::isScenePlaying;

    if (UiManager::isScenePlaying) {
        UiManager::enterPlayMode();
        ui->playSceneBtn->setToolTip("Stop playing");
        ui->playSceneBtn->setIcon(QIcon(":/icons/g_stop.svg"));
    }
    else {
        UiManager::enterEditMode();
        ui->playSceneBtn->setToolTip("Play scene");
        ui->playSceneBtn->setIcon(QIcon(":/icons/g_play.svg"));
    }
}
