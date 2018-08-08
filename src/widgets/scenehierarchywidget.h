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
#include <QHash>
#include <QIcon>
#include <QEvent>
#include <QTreeWidget>
#include <QLineEdit>
#include <QStyledItemDelegate>

#include <qcombobox.h>
#include "irisgl/src/irisglfwd.h"
#include "irisgl/src/scenegraph/scenenode.h"

#include "core/project.h"

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

class TreeItemDelegate : public QStyledItemDelegate
{
public:
    TreeItemDelegate(QObject *parent = nullptr) : QStyledItemDelegate(parent) {}
    void setModelData(QWidget *editor, QAbstractItemModel *model,
        const QModelIndex &index) const
    {
        QLineEdit *lineEdit = qobject_cast<QLineEdit *>(editor);
        if (!lineEdit->isModified()) {
            return;
        }
        QString text = lineEdit->text();
        text = text.trimmed();
        if (text.isEmpty()) {
            // If text is empty, do nothing - preserve the old value.
            return;
        }
        else {
            QStyledItemDelegate::setModelData(editor, model, index);
        }
    }
};

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

    void OnLstItemsCommitData(QWidget *listItem);
    QComboBox *box;

    QTreeWidget *getWidget();

protected:
    bool eventFilter(QObject *watched, QEvent *event);

protected slots:
    void treeItemSelected(QTreeWidgetItem *item, int column);
    void sceneTreeCustomContextMenu(const QPoint &);

    void constraintsPicked(int index, iris::PhysicsConstraintType type);

    void deleteNode();
	void duplicateNode();
	void focusOnNode();
	void exportNode(const iris::SceneNodePtr &node, ModelTypes modelType);
	void createMaterial();
	void exportParticleSystem(const iris::SceneNodePtr &node);

	void attachAllChildren();
	void detachFromParent();

private:
	void showHideNode(QTreeWidgetItem* item, bool show);
    void repopulateTree();
    void populateTree(QTreeWidgetItem* parentNode,QSharedPointer<iris::SceneNode> sceneNode);

    QTreeWidgetItem* createTreeItems(iris::SceneNodePtr node);

    // maps scene nodes to their widgetitems
    QMap<long, QSharedPointer<iris::SceneNode>> nodeList;
    QMap<long, QTreeWidgetItem*> treeItemList;

    QSharedPointer<iris::SceneNode> lastDraggedHiearchyItemSrc;

	void hideItemAndChildren(QTreeWidgetItem* item);
	void showItemAndChildren(QTreeWidgetItem* item);
	void lockItemAndChildren(QTreeWidgetItem* item);
	void releaseItemAndChildren(QTreeWidgetItem* item);

	// attachment

	// sets the `attached` property of all children nodes to true
	// updates the ui accordingly
	void attachAllChildren(iris::SceneNodePtr node);
	void _attachAllChildren(iris::SceneNodePtr node);

	// detach a node from its parent and update the treenode ui
	void detachFromParent(iris::SceneNodePtr node);

	void refreshAttachmentColors(iris::SceneNodePtr node);

private:
    Ui::SceneHierarchyWidget *ui;
    QSharedPointer<iris::Scene> scene;
    QSharedPointer<iris::SceneNode> selectedNode;
    MainWindow* mainWindow;

	QIcon *visibleIcon;
	QIcon *hiddenIcon;
	QIcon *pickableIcon;
	QIcon *disabledIcon;

signals:
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);
};

#endif // SCENEHEIRARCHYWIDGET_H
