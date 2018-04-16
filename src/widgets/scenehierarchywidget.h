/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEHIERARCHYWIDGET_H
#define SCENEHIERARCHYWIDGET_H

#include <QWidget>
#include <QMap>
#include <QIcon>
#include <QEvent>
#include "../irisgl/src/irisglfwd.h"

namespace Ui {
class SceneHierarchyWidget;
}

namespace iris
{
    class Scene;
    class SceneNode;
}

class QTreeWidgetItem;
class MainWindow;

class SceneHierarchyWidget : public QWidget
{
    Q_OBJECT

    friend class MainWindow;
public:
    explicit SceneHierarchyWidget(QWidget *parent = 0);
    ~SceneHierarchyWidget();

    void setScene(QSharedPointer<iris::Scene> scene);
    void setMainWindow(MainWindow* mainWin);

    void setSelectedNode(QSharedPointer<iris::SceneNode> sceneNode);

    /**
     * @brief Inserts item into tree under the parent
     * This function assumes the child node is already a part of the scene and has a parent
     * already displayed in the scenetree
     * The node will be added to the nodeList and new tree item added to the treeItemList
     * This function should be used when a new node is created and needs to be added to the
     * scene tree heirarchy without having to repopulate the entire scene tree.
     * @param childNode
     */
    void insertChild(iris::SceneNodePtr childNode);

    /**
     * @brief removeChild
     * This function is the opposite of insertChild.
     * It removes a child node's tree item from the scene heirarchym, nodeList and treeItemList
     * THIS FUNCTION DOES NOT REMOVE THE childNode FROM ITS PARENT SCENE NODE
     * @param childNode
     */
    void removeChild(iris::SceneNodePtr childNode);

protected:
    bool eventFilter(QObject *watched, QEvent *event);

protected slots:
    void treeItemSelected(QTreeWidgetItem *item, int column);
    void sceneTreeCustomContextMenu(const QPoint &);

    void renameNode();
    void deleteNode();
	void duplicateNode();
	void focusOnNode();
	void exportNode(const QString &guid);
	void createMaterial(const QString &guid);
	void exportParticleSystem(const QString &guid);

private:
	void showHideNode(QTreeWidgetItem* item, bool show);
    void repopulateTree();
    void populateTree(QTreeWidgetItem* parentNode,QSharedPointer<iris::SceneNode> sceneNode);

    QTreeWidgetItem* createTreeItems(iris::SceneNodePtr node);

    // maps scene nodes to their widgetitems
    QMap<long, QSharedPointer<iris::SceneNode>> nodeList;
    QMap<long, QTreeWidgetItem*> treeItemList;

    QSharedPointer<iris::SceneNode> lastDraggedHiearchyItemSrc;

private:
    Ui::SceneHierarchyWidget *ui;
    QSharedPointer<iris::Scene> scene;
    QSharedPointer<iris::SceneNode> selectedNode;
    MainWindow* mainWindow;

	QIcon *visibleIcon;
	QIcon *hiddenIcon;

signals:
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);
protected:
	void changeEvent(QEvent* event) override;
};

#endif // SCENEHEIRARCHYWIDGET_H
