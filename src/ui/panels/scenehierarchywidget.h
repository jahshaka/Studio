/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016-2026 EXEDOS LLC (www.exedos.com)

This is free software: you may copy, redistribute
and/or modify it under the terms of the MIT License

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
#include <QStringList>
#include <functional>

#include "ui/panels/scenetreewidget.h"

#include <qcombobox.h>
#include "irisgl/irisglfwd.h"
#include "ui/style/thememanager.h"
#include "irisgl/document/scenegraph/scenenode.h"

#include "data/project.h"

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

    // Qlementine's item-view metrics are airy for a dense scene tree: halve
    // the vertical padding around the row text, and halve the horizontal
    // padding around the icon-only eye/lock columns so more of the node name
    // fits at the same panel width. Scoped to this tree via its delegate —
    // not an app-wide density change. Classic keeps its own metrics.
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override
    {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        if (ThemeManager::classicActive())
            return size;

        const int textHeight = option.fontMetrics.height();
        if (size.height() > textHeight)
            size.setHeight(textHeight + (size.height() - textHeight) / 2);

        if (index.column() > 0) {
            const int iconWidth = option.decorationSize.width();
            if (size.width() > iconWidth)
                size.setWidth(iconWidth + (size.width() - iconWidth) / 2);
        }
        return size;
    }
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

	void selectNode(QString nodeId);

    /// Rebuilds the whole tree from the document. Public for the undo commands
    /// (reparent, folder edits) that change the document behind the widget's back.
    void repopulateTree();

    // ---- OUTLINER FOLDERS (SCENEGRAPH_SPEC §6b) ---------------------------
    //
    // A folder is a ROW, never a node: it has no guid, no transform and no
    // place in the scene hierarchy, and the only thing that makes a row a
    // folder is that it carries kFolderRole. Node rows carry a nodeId in
    // Qt::UserRole exactly as they always did, so every existing walk keeps
    // working — as long as it SKIPS rows that answer isFolderItem(), which is
    // the one new rule in this file (a folder row's nodeId reads back as 0, and
    // nodeList[0] is a null node pointer waiting to be dereferenced).
    static constexpr int kFolderRole = Qt::UserRole + 10;
    static bool isFolderItem(const QTreeWidgetItem *item);
    static QString folderPathOf(const QTreeWidgetItem *item);

    /// The nodes currently selected, in tree order, folder rows excluded.
    /// Multi-select drives folder-ising and delete; the LAST selected row is
    /// what drives the properties panel and the gizmo (§6b).
    QList<iris::SceneNodePtr> selectedNodes() const;

protected:
    bool eventFilter(QObject *watched, QEvent *event);

protected slots:
    void treeItemSelected(QTreeWidgetItem *item, int column);
    void treeSelectionChanged();
    void sceneTreeCustomContextMenu(const QPoint &);

    /// Toolbar folder button: a folder from the current multi-selection, or an
    /// empty folder when nothing is selected. NEVER reparents (§6b LAW).
    void newFolderFromSelection();

    void constraintsPicked(int constraintGuidToIndex, iris::PhysicsConstraintType type);

    void deleteNode();
	void duplicateNode();
	void focusOnNode();
	void exportNode(const iris::SceneNodePtr &node, ModelTypes modelType);
	void createMaterial();
	void exportParticleSystem(const iris::SceneNodePtr &node);

	void attachAllChildren();
	void detachFromParent();

private:
    // ---- folders ----------------------------------------------------------
    iris::ScenePtr documentScene() const { return scene; }
    /// Applies `fn` (a folder edit on the scene) as ONE undo step and rebuilds.
    void runFolderEdit(const QString &text, const std::function<bool()> &fn);
    /// Files every node in `nodes` under `path` as one undo step.
    void moveNodesToFolder(const QList<iris::SceneNodePtr> &nodes, const QString &path);
    /// Builds (or finds) the row for a folder path, creating ancestors.
    QTreeWidgetItem *folderItemFor(const QString &path);
    /// Asks for a folder name; empty return = cancelled.
    QString askFolderName(const QString &title, const QString &initial);
    /// The "Move to ▸" submenu — every folder, New Folder…, and Root.
    void buildMoveToMenu(QMenu *parent, const QList<iris::SceneNodePtr> &nodes);
    /// Can this node be FILED at all? Folders organise the root level, so only
    /// a direct child of the world root can be in one (§6b, Unreal semantics).
    bool isFolderable(const iris::SceneNodePtr &node) const;
    /// What a drop of `dragged` at `pos` would do, and on which row.
    ///
    /// The dragged set is a PARAMETER and not the member on purpose: the drop
    /// handler has to take its own copy and clear the member before it does
    /// anything else (a drop that repopulates the tree must not leave a stale
    /// selection behind), and reading the member here silently answered "None"
    /// for every drop — caught on the Xvfb rig, 2026-09-06, by a folder drop
    /// that changed nothing.
    SceneTreeWidget::DropHint dropHintAt(const QList<iris::SceneNodePtr> &dragged,
                                         const QPoint &pos, QTreeWidgetItem **rowOut,
                                         QString *folderOut) const;

	void showHideNode(QTreeWidgetItem* item, bool show);
    void populateTree(QTreeWidgetItem* parentNode,QSharedPointer<iris::SceneNode> sceneNode);

    QTreeWidgetItem* createTreeItems(iris::SceneNodePtr node);

    // maps scene nodes to their widgetitems
    // Keyed by iris::SceneNode::nodeId, which is qint64 (it was `long`, i.e.
    // 32-bit on Windows LLP64).
    QMap<qint64, QSharedPointer<iris::SceneNode>> nodeList;
    QMap<qint64, QTreeWidgetItem*> treeItemList;

    /// The dragged selection, captured on DragEnter. Multi-select made this a
    /// LIST — the single-node member it replaced could only ever move the first
    /// row of a selection.
    QList<iris::SceneNodePtr> lastDraggedNodes;
    /// Folder path -> its row. Rebuilt by repopulateTree.
    QHash<QString, QTreeWidgetItem*> folderItemList;
    /// Guards setSelectedNode() against re-emitting the selection it was just
    /// given (the panel drives the viewport and the viewport drives the panel).
    bool suppressSelectionSignal = false;
    /// Folder rows that were collapsed, so a repopulate does not expand them all.
    QStringList collapsedFolders;

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
