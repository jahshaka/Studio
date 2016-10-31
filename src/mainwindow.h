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
#include <QModelIndex>
#include <QDropEvent>
#include <QMimeData>
//#include "editor/gizmos/advancedtransformgizmo.h"

namespace Ui {
class MainWindow;
class NewMainWindow;
}

class SurfaceView;

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

class SceneNodePtr;
class SceneManagerPtr;

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
    //void setGizmoTransformMode(GizmoTransformMode mode);

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
    //void setupPropertyTabs(SceneNode* node);

    void setupLayerManager();

    void rebuildTree();
    //void populateTree(QStandardItem* treeNode,SceneNode* sceneNode);
    //void populateTree(QTreeWidgetItem* treeNode,SceneNode* sceneNode);
    void deselectTreeItems();

    //void addSceneNodeToSelectedTreeItem(QTreeWidget* sceneTree,SceneNode* newNode,bool addToSelected,QIcon icon);

    void setupDefaultScene();

    void resizeEvent(QResizeEvent* event);

    QIcon getIconFromSceneNodeType(SceneNodeType type);

    void removeScene();
    void setScene(SceneManagerPtr scene);
    void updateGizmoTransform();

    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    void dragLeaveEvent(QDragLeaveEvent* event) override;

    //SceneNodePtr getSceneNodeByEntityId(Qt3DCore::QNodeId nodeId);

    void assignPickerRecursively(SceneNodePtr node);

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

    void setActiveSceneNode(SceneNodePtr node);

    void createSceneNodeContextMenu(QMenu& menu,SceneNodePtr node);

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
    QStandardItemModel* treeModel;
    QWidget *container;
    EditorCameraController* camControl;

    //SceneManagerPtr scene;
    //SceneNodePtr activeSceneNode;

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

    SettingsManager* settings;
    PreferencesDialog* prefsDialog;
    LicenseDialog* licenseDialog;
    AboutDialog* aboutDialog;
};

#endif // MAINWINDOW_H
