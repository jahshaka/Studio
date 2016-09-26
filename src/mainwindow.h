/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QModelIndex>
#include "scenemanager.h"
#include <Qt3DRender/QCamera>
#include <QDropEvent>
#include <QMimeData>
#include "advancedtransformgizmo.h"
//#include "layermanager.h"

namespace Ui {
class MainWindow;
class NewMainWindow;
}

class SurfaceView;

namespace Qt3DCore{
    class QEntity;
    class QAspectEngine;
}

namespace Qt3DRender
{
    class QLayerFilter;
    class QRenderSurfaceSelector;
    class QRenderSettings;
    class QPickEvent;
}

namespace Qt3DExtras
{
    class QForwardRenderer;
}

namespace Qt3DInput
{
    class QMouseEvent;
    class QInputSettings;
}

class QStandardItem;
class QStandardItemModel;
class QTreeWidgetItem;
class QTreeWidget;
class QIcon;

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

class MaterialWidget;
class TransformGizmo;
class AdvancedTransformGizmo;
class TransformWidget;

class EditorCameraController;
class SettingsManager;
class PreferencesDialog;
class LicenseDialog;
class AboutDialog;

class JahRenderer;

class GizmoHitData;
class AdvancedGizmoHandle;

enum class SceneNodeType;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = 0);
    ~MainWindow();

    void setProjectTitle(QString projectTitle);

    void setSceneAnimTime(float time);
    void stopAnimWidget();

    bool handleMousePress(QMouseEvent *event);
    bool handleMouseRelease(QMouseEvent *event);
    bool handleMouseMove(QMouseEvent *event);
    bool handleMouseWheel(QWheelEvent *event);
    bool eventFilter(QObject *obj, QEvent *event);

    void setSettingsManager(SettingsManager* settings);
    SettingsManager* getSettingsManager();

    void openProject(QString project);
    void setGizmoTransformMode(GizmoTransformMode mode);

private:
    void setupQt3d();

    //menus
    void setupFileMenu();
    void setupViewMenu();
    void setupHelpMenu();

    //ui setup
    void setupLayerButtonMenu();
    void initLightLayerUi();
    void initTorusLayerUi();

    void setupPropertyUi();
    void setupPropertyTabs(SceneNode* node);

    void setupLayerManager();

    void rebuildTree();
    void populateTree(QStandardItem* treeNode,SceneNode* sceneNode);
    void populateTree(QTreeWidgetItem* treeNode,SceneNode* sceneNode);
    void deselectTreeItems();

    void addSceneNodeToSelectedTreeItem(QTreeWidget* sceneTree,SceneNode* newNode,bool addToSelected,QIcon icon);

    void setupDefaultScene();

    void resizeEvent(QResizeEvent* event);

    QIcon getIconFromSceneNodeType(SceneNodeType type);

    void removeScene();
    void setScene(SceneManager* scene);
    void updateGizmoTransform();

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;

    SceneNode* getSceneNodeByEntityId(Qt3DCore::QNodeId nodeId);

    void assignPickerRecursively(SceneNode* node);

private slots:
    //scenegraph
    void addCube();
    void addTorus();
    void addSphere();
    void addCylinder();
    void addMesh();
    void addTexturedPlane();
    void addViewPoint();

    //context menu functions
    void duplicateNode();
    void deleteNode();
    void renameNode();

    void addPointLight();
    void addSpotLight();
    void addDirectionalLight();

    void updateAnim();

    void sceneNodeSelected(QTreeWidgetItem* item);
    void sceneTreeCustomContextMenu(const QPoint&);
    void sceneTreeItemChanged(QTreeWidgetItem* item,int column);

    void setActiveSceneNode(SceneNode* node);
    void makeSceneNodePickable(SceneNode* entity);

    void entityPicked(Qt3DRender::QPickEvent* event);
    void createSceneNodeContextMenu(QMenu& menu,SceneNode* node);

    //TORUS SLIDERS
    void sliderTorusMinorRadius(int val);//float
    void sliderTorusRings(int val);
    void sliderTorusSlices(int val);

    void saveScene();
    void saveSceneAs();
    void loadScene();
    void openRecentFile();

    void useEditorCamera();
    void useUserCamera();

    void showPreferences();
    void exitApp();
    void newScene();

    void showAboutDialog();
    void showLicenseDialog();
    void openBlogUrl();
    void openWebsiteUrl();

private:
    Ui::NewMainWindow *ui;
    SurfaceView* surface;
    Qt3DCore::QEntity* rootEntity;
    Qt3DCore::QAspectEngine* engine;
    QStandardItemModel* treeModel;
    Qt3DInput::QInputAspect *input;
    QWidget *container;
    Qt3DRender::QCamera* editorCam;
    EditorCameraController* camControl;

    JahRenderer* jahRenderer;
    Qt3DExtras::QForwardRenderer* forwardRenderer;
    Qt3DRender::QRenderSurfaceSelector* renderSurfaceSelector;
    Qt3DRender::QLayerFilter* sceneLayerFilter;
    Qt3DRender::QLayerFilter* billboardLayerFilter;
    Qt3DRender::QLayerFilter* gizmoLayerFilter;

    Qt3DRender::QRenderSettings* renderSettings;
    Qt3DInput::QInputSettings* inputSettings;

    SceneManager* scene;

    SceneNode* activeSceneNode;
    //TransformGizmo* gizmo;


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

    Qt::MouseButton mouseButton;
    QPoint mousePressPos;
    QPoint mouseReleasePos;
    QPoint mousePos;

    //QMap<Qt3DCore::QNodeId, SceneNode*> sceneNodes;
    //this map must be in sync with the treewidget
    QMap<Qt3DCore::QNodeId, QTreeWidgetItem*> sceneTreeItems;

    SettingsManager* settings;
    PreferencesDialog* prefsDialog;
    LicenseDialog* licenseDialog;
    AboutDialog* aboutDialog;

    //gizmo stuff
    AdvancedTransformGizmo* gizmo;
    GizmoHitData* gizmoHitData;
    //last gizmo title
    //AdvancedGizmoHandle* gizmoHandle;

    //current gizmo handle being selected
    AdvancedGizmoHandle* activeGizmoHandle;
};

#endif // MAINWINDOW_H
