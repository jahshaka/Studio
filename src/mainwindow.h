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
#include <QDrag>
#include <QSharedPointer>
#include <QVector3D>
#include "irisgl/src/irisglfwd.h"

namespace Ui {
    class MainWindow;
}

class SurfaceView;

class QPushButton;
class QStandardItem;
class QStandardItemModel;
class QTreeWidgetItem;
class QTreeWidget;
class QIcon;
class QUndoStack;

//custom ui
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

class MaterialWidget;
class TransformGizmo;
class AdvancedTransformGizmo;
class TransformWidget;

class SceneViewWidget;

class EditorCameraController;
class SettingsManager;
class PreferencesDialog;
class LicenseDialog;
class AboutDialog;

class JahRenderer;

class ProjectManager;

class GizmoHitData;
class AdvancedGizmoHandle;
class MaterialPreset;
class AssetWidget;

class QOpenGLFunctions_3_2_Core;

enum class SceneNodeType;

class Database;
#include <QDebug>
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void setSceneAnimTime(float time);
    void stopAnimWidget();

    void initialize();
    void setupProjectDB();
    void setupUndoRedo();

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

    void addNodeToActiveNode(QSharedPointer<iris::SceneNode> sceneNode);
    void addNodeToScene(QSharedPointer<iris::SceneNode> sceneNode, bool ignore = false);
    void repopulateSceneTree();

private:

    // sets up the button for vr
    void setupVrUi();

    // menus
    void setupFileMenu();
    void setupViewMenu();
    void setupHelpMenu();

    //ui setup
    void createPostProcessDockWidget();
    void setupLayerButtonMenu();
    void initLightLayerUi();
    void initTorusLayerUi();

    void setupPropertyUi();
    //void setupPropertyTabs(SceneNode* node);

    void setupLayerManager();

    void rebuildTree();
    //void populateTree(QStandardItem* treeNode,SceneNode* sceneNode);
    //void populateTree(QTreeWidgetItem* treeNode,SceneNode* sceneNode);
    void deselectTreeItems();

    void setupDefaultScene();

    void resizeEvent(QResizeEvent* event);

    QIcon getIconFromSceneNodeType(SceneNodeType type);

    void removeScene();
    void setScene(QSharedPointer<iris::Scene> scene);
    void updateGizmoTransform();    // @TODO - move this into updateSceneSettings

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;

    // determines if file extension is that of a model (obj, fbx, 3ds)
    bool isModelExtension(QString extension);

public slots:
    //scenegraph
    void addPlane();
    void addCone();
    void addCube();
    void addTorus();
    void addSphere();
    void addCylinder();
    void addEmpty();
    void addViewer();
    void addMesh(const QString &path = "", bool ignore = false, QVector3D position = QVector3D());
    void addTexturedPlane();
    void addViewPoint();
    void addDragPlaceholder();

    //context menu functions
    void duplicateNode();
    void deleteNode();
    void renameNode();

    void addPointLight();
    void addSpotLight();
    void addDirectionalLight();

    void addParticleSystem();

    void updateAnim();

    void sceneNodeSelected(QTreeWidgetItem* item);
    void sceneTreeCustomContextMenu(const QPoint&);
    void sceneTreeItemChanged(QTreeWidgetItem* item,int column);

    void sceneNodeSelected(iris::SceneNodePtr sceneNode);

    //TORUS SLIDERS
    void saveScene();
    void saveSceneAs();
    void loadScene();
//    QString loadSceneDelegate();
    void openRecentFile();

    void showPreferences();
    void exitApp();
    void newScene();
    void newSceneProject();
    void deleteProject();
    void callProjectManager();
    void projectManager(bool mainWindowActive = false);
    void newProject(const QString&, const QString&);

    void showAboutDialog();
    void showLicenseDialog();
    void openFacebookUrl();
    void openWebsiteUrl();

    void openProject(QString project);

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
    void tabsChanged(int);
    void showProjectManager();

private slots:
    void translateGizmo();
    void rotateGizmo();
    void scaleGizmo();

    void onPlaySceneButton();

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

    SettingsManager* settings;
    PreferencesDialog* prefsDialog;
    LicenseDialog* licenseDialog;
    AboutDialog* aboutDialog;

    QActionGroup* transformGroup;
    QActionGroup* transformSpaceGroup;
    QActionGroup* cameraGroup;

    Database *db;
    AssetWidget *assetWidget;
    ProjectManager *pmContainer;

    QUndoStack* undoStack;

    bool vrMode;
    QPushButton* vrButton;
    QPushButton* pmButton;
    QMainWindow *dialog;
};

#endif // MAINWINDOW_H
