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
#include "irisglfwd.h"
#include "misc/QtAwesome.h"
#include "misc/QtAwesomeAnim.h"

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
    void applyMaterialPreset(MaterialPreset* preset);

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

private:

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
    void addMesh(const QString &path = "", bool ignore = false, QVector3D position = QVector3D());
	void addMaterialMesh(const QString &path = "", bool ignore = false, QVector3D position = QVector3D(), const QString &guid = QString(), const QString &name = QString());
    void addDragPlaceholder();

    //context menu functions
    void duplicateNode();
	void createMaterial();
	void exportNode(const iris::SceneNodePtr &node);
	void exportNodes(const QStringList &assetGuids);
    void deleteNode();
    void renameNode();

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
    void showProjectManagerInternal();

private slots:
    void translateGizmo();
    void rotateGizmo();
    void scaleGizmo();

    void onPlaySceneButton();
    void enterEditMode();
    void enterPlayMode();

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
    QCheckBox *wireCheckBtn;
    QPushButton *restartBtn;
    QPushButton *playBtn;
    QPushButton *stopBtn;

    QToolBar *toolBar;
    AssetView *_assetView;
	QAction *actionSaveScene;

	QVector<bool> widgetStates;	// use the order in the enum

    WindowSpaces currentSpace;
	QtAwesome fontIcons;
};

#endif // MAINWINDOW_H
