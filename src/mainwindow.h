/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QActionGroup>
#include <QModelIndex>
#include <QDropEvent>
#include <QMimeData>
#include <QListWidgetItem>
#include <QDrag>
#include <QToolBar>
#include <QSharedPointer>
#include <QVector3D>
#include <QLabel>
#include <QCheckBox>
#include <QMenu>
#include "irisglfwd.h"
#include "misc/QtAwesome.h"
#include "misc/QtAwesomeAnim.h"
#include "core/project.h"

#ifdef MINER_ENABLED
#include "../thirdparty/miner/minerui.h"
#endif

namespace Ui {
    class MainWindow;
}

class SurfaceView;
class AssetView;

class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTreeWidgetItem;
class QTreeWidget;
class QIcon;
class QUndoStack;
class QToolButton;

class TransformSlidersUi;
class LightLayerWidget;
class ModelLayerWidget;
class TorusLayerWidget;
class SphereLayerWidget;
class TimelineWidget;
class KeyFrameWidget;
class AnimationWidget;
class TexturedPlaneLayerWidget;
class WorldLayerWidget;
class EndlessPlaneLayerWidget;
class PostProcessesWidget;
class DonateDialog;

class MaterialWidget;
class TransformGizmo;
class AdvancedTransformGizmo;
class TransformWidget;

class SceneViewWidget;
class SceneHierarchyWidget;

class EditorCameraController;
class SettingsManager;
class PreferencesDialog;
class AboutDialog;

class JahRenderer;

class ProjectManager;

class GizmoHitData;
class AdvancedGizmoHandle;
class MaterialPreset;
class AssetWidget;
// class SceneNodePropertiesWidget;

class AssetModelPanel;
class AssetMaterialPanel;

class MinerUI;

#include "widgets/scenenodepropertieswidget.h"

class QOpenGLFunctions_3_2_Core;

enum class SceneNodeType;

enum WindowSpaces {
    DESKTOP,
    PLAYER,
    EDITOR,
    ASSETS
};

enum class Widget
{
	HIERARCHY,
	PROPERTIES,
	ASSETS,
	TIMELINE,
	PRESETS
};

#include <QJsonObject>
#include "irisgl/src/scenegraph/lightnode.h"
#include "irisgl/src/graphics/shadowmap.h"
#include "irisgl/src/core/irisutils.h"
#include "irisgl/src/graphics/texture2d.h"

class Database;
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void setSceneAnimTime(float time);
    void stopAnimWidget();

    void grabOpenGLContextHack();
    void goToDesktop();
    void setupProjectDB();
    void setupUndoRedo();

	WindowSpaces getWindowSpace();
	void deselectViewports();
    void switchSpace(WindowSpaces space);

    bool handleMousePress(QMouseEvent *event);
    bool handleMouseRelease(QMouseEvent *event);
    bool handleMouseMove(QMouseEvent *event);
    bool handleMouseWheel(QWheelEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);

    virtual void closeEvent(QCloseEvent *event);

    void setSettingsManager(SettingsManager* settings);
    SettingsManager* getSettingsManager();

    iris::ScenePtr getScene();

    //void setGizmoTransformMode(GizmoTransformMode mode);

    /**
     * Applies material preset to active scene node and refreshes material property widget
     * @param preset
     */
    void applyMaterialPreset(QString guid);
    void applyMaterialPreset(MaterialPreset preset);

    void favoriteItem(QListWidgetItem *item);
    void refreshThumbnail(const QString &guid);
    void refreshThumbnail(QListWidgetItem *item);

    /**
     * Returns absolute path of file copied as an asset
     * @param relToApp file path relative to application
     * @return
     */
    QString getAbsoluteAssetPath(QString pathRelativeToApp);
    QString originalTitle;

    void addNodeToActiveNode(QSharedPointer<iris::SceneNode> sceneNode);
    void addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, bool ignore = false);
    void repopulateSceneTree();

    iris::ShadowMapType evalShadowMapType(QString shadowType)
    {
        if (shadowType == "hard")
            return iris::ShadowMapType::Hard;
        if (shadowType == "soft")
            return iris::ShadowMapType::Soft;
        if (shadowType == "softer")
            return iris::ShadowMapType::Softer;

        return iris::ShadowMapType::None;
    }

    iris::LightType getLightTypeFromName(QString lightType)
    {
        if (lightType == "point")       return iris::LightType::Point;
        if (lightType == "directional") return iris::LightType::Directional;
        if (lightType == "spot")        return iris::LightType::Spot;

        return iris::LightType::Point;
    }

    iris::LightNodePtr createLight(QJsonObject& nodeObj)
    {
        auto lightNode = iris::LightNode::create();

        lightNode->setLightType(getLightTypeFromName(nodeObj["lightType"].toString()));
        lightNode->intensity = (float) nodeObj["intensity"].toDouble(1.0f);
        lightNode->distance = (float) nodeObj["distance"].toDouble(1.0f);
        lightNode->spotCutOff = (float) nodeObj["spotCutOff"].toDouble(30.0f);
        lightNode->color = IrisUtils::readColor(nodeObj["color"].toObject());
        lightNode->setVisible(nodeObj["visible"].toBool(true));

        //shadow data
        auto shadowMap = lightNode->shadowMap;
        shadowMap->bias = (float) nodeObj["shadowBias"].toDouble(0.0015f);
        // ensure shadow map size isnt too big ro too small
        auto res = qBound(512, nodeObj["shadowSize"].toInt(1024), 4096);
        shadowMap->setResolution(res);
        shadowMap->shadowType = evalShadowMapType(nodeObj["shadowType"].toString());

        //TODO: move this to the sceneview widget or somewhere more appropriate
        if (lightNode->lightType == iris::LightType::Directional) {
            lightNode->icon = iris::Texture2D::load(":/icons/light.png");
        }
        else {
            lightNode->icon = iris::Texture2D::load(":/icons/bulb.png");
        }

        lightNode->iconSize = 0.5f;

        return lightNode;
    }

private:
	//set up miner
#ifdef MINER_ENABLED
	MinerUI *miner;
	void configureMiner();
#endif

    // sets up the button for vr
    void setupVrUi();

    // menus
    void setupFileMenu();

    //ui setup
    void createPostProcessDockWidget();
    void setupLayerButtonMenu();
    void initLightLayerUi();
    void initTorusLayerUi();

    void setupPropertyUi();

    void setupLayerManager();

    void rebuildTree();
    void deselectTreeItems();

    void setupDefaultScene();

    QIcon getIconFromSceneNodeType(SceneNodeType type);

    void removeScene();
    void setScene(QSharedPointer<iris::Scene> scene);
    void updateGizmoTransform();    // @TODO - move this into updateSceneSettings

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;

    void updateCurrentSceneThumbnail();

    // determines if file extension is that of a model (obj, fbx, 3ds)
    // bool isModelExtension(QString extension);

public slots:
    void exportSceneAsZip();

    void setupDockWidgets();
    void setupViewPort();
    void setupDesktop();
    void setupToolBar();
    void setupShortcuts();

    //scenegraph
    void addPlane();
    void addGround();
    void addCapsule();
    void addCone();
    void addCube();
    void addTorus();
    void addSphere();
    void addCylinder();
    void addPyramid();
    void addTeapot();
    void addSponge();
    void addSteps();
    void addGear();
    void addEmpty();
    void addViewer();
	void addGrabHand();
    void addMesh(const QString &path = "", bool ignore = false, QVector3D position = QVector3D());
    void addPrimitiveObject(const QString &guid);
	void addMaterialMesh(const QString &path = "", bool ignore = false, QVector3D position = QVector3D(), const QString &guid = QString(), const QString &name = QString());
    void addAssetParticleSystem(bool ignore, QVector3D position, QString guid, QString assetName);
    void addDragPlaceholder();

    //context menu functions
    void duplicateNode();
	void createMaterial();
	void exportNode(const iris::SceneNodePtr &node, ModelTypes modelType);
    void deleteNode();

    void addPointLight();
    void addSpotLight();
    void addDirectionalLight();

    void addParticleSystem();

    void updateAnim();

    void sceneTreeCustomContextMenu(const QPoint&);
    void sceneTreeItemChanged(QTreeWidgetItem* item,int column);

    void sceneNodeSelected(QTreeWidgetItem *item);
    void assetItemSelected(QListWidgetItem *item);
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);

	void saveScene(const QString &filename, const QString &projectPath);
    void saveScene();

	void toggleDockWidgets();
    void showPreferences();
    void exitApp();
    void newScene();

    void newProject(const QString&, const QString&);
    void openProject(bool playMode = false);
    void closeProject();

    void toggleWidgets(bool toggle);

    iris::ScenePtr createDefaultScene();
    void initializeGraphics(SceneViewWidget*, QOpenGLFunctions_3_2_Core*);

    void initializePhysicsWorld();

    void useFreeCamera();
    void useArcballCam();

    void useLocalTransform();
    void useGlobalTransform();

    void vrButtonClicked(bool);
    void updateSceneSettings();

    void undo();
    void redo();

    void takeScreenshot();
    void toggleLightWires(bool state);
    void toggleDebugDrawer(bool state);
    void showProjectManagerInternal();

signals:
	void projectionChangeRequested(bool val);

private slots:
    void translateGizmo();
    void rotateGizmo();
    void scaleGizmo();

    void onPlaySceneButton();
    void enterEditMode();
    void enterPlayMode();

	void changeProjection(bool val);

private:
    Ui::MainWindow *ui;
    SurfaceView* surface;
    SceneViewWidget* sceneView;

    QStandardItemModel* treeModel;
    QWidget *container;
    EditorCameraController* camControl;

    QSharedPointer<iris::Scene> scene;
    QSharedPointer<iris::SceneNode> activeSceneNode;

    QTimer* timer;

    TransformWidget* transformUi;

    LightLayerWidget* lightLayerWidget;
    ModelLayerWidget* modelLayerWidget;
    TorusLayerWidget* torusLayerWidget;
    SphereLayerWidget* sphereLayerWidget;
    TexturedPlaneLayerWidget* texPlaneLayerWidget;
    WorldLayerWidget* worldLayerWidget;
    EndlessPlaneLayerWidget* endlessPlaneLayerWidget;
    MaterialWidget* materialWidget;
    AnimationWidget* animWidget;
    PostProcessesWidget* postProcessWidget;
    QDockWidget* postProcessDockWidget;

    Qt::MouseButton mouseButton;
    QPoint mousePressPos;
    QPoint mouseReleasePos;
    QPoint mousePos;
    bool dragging;
    QVector3D dragScenePos;

	int undoStackCount;
    SettingsManager* settings;
    PreferencesDialog* prefsDialog;
    AboutDialog* aboutDialog;

    QActionGroup* transformGroup;
    QActionGroup* transformSpaceGroup;
    QActionGroup* cameraGroup;

    Database *db;
    ProjectManager *pmContainer;

    QUndoStack* undoStack;

	QPushButton *worlds_menu;
	QPushButton *player_menu;
	QPushButton *editor_menu;
	QPushButton *assets_menu;
	QWidget *assets_panel;
	QLabel *jlogo;
	QPushButton *help;
	QPushButton *prefs;

    bool vrMode;
    QPushButton* vrButton;
    QMainWindow *dialog;

    QDockWidget *sceneHierarchyDock;
    SceneHierarchyWidget *sceneHierarchyWidget;

    QDockWidget *sceneNodePropertiesDock;
    SceneNodePropertiesWidget *sceneNodePropertiesWidget;

    QDockWidget *presetsDock;
    QTabWidget *presetsTabWidget;

    QDockWidget *assetDock;
    AssetWidget *assetWidget;

    QDockWidget *animationDock;
    AnimationWidget *animationWidget;

    QMainWindow *viewPort;
    QWidget *sceneContainer;

    QWidget *controlBar;
    QWidget *playerControls;
    QPushButton *playSceneBtn;
    QMenu *wireFramesMenu;
    QCheckBox *wireCheckBtn;
    QToolButton *wireFramesButton;
    QPushButton *restartBtn;
    QPushButton *playBtn;
    QPushButton *stopBtn;

    QToolBar *toolBar;
    AssetView *_assetView;
	QAction *actionSaveScene;

    QAction *wireCheckAction;
    QAction *physicsCheckAction;

	QVector<bool> widgetStates;	// use the order in the enum

    WindowSpaces currentSpace;
	QtAwesome fontIcons;

	QPushButton *playSimBtn;

    QAction *actionTranslate;
    QAction *actionRotate;
    QAction *actionScale;

    AssetModelPanel *assetModelPanel;
    AssetMaterialPanel *assetMaterialPanel;

	QPushButton *cameraView;
};

#endif // MAINWINDOW_H
