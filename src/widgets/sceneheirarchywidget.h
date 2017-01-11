/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SCENEHEIRARCHYWIDGET_H
#define SCENEHEIRARCHYWIDGET_H

#include <QWidget>
#include <QMap>
#include <QEvent>
#include "../irisgl/src/irisglfwd.h"

namespace Ui {
class SceneHeirarchyWidget;
}

namespace iris
{
    class Scene;
    class SceneNode;
}

class QTreeWidgetItem;
class MainWindow;

class SceneHeirarchyWidget : public QWidget
{
    Q_OBJECT

    friend class MainWindow;
public:
    explicit SceneHeirarchyWidget(QWidget *parent = 0);
    ~SceneHeirarchyWidget();

    void setScene(QSharedPointer<iris::Scene> scene);
    void setMainWindow(MainWindow* mainWin);

    void setSelectedNode(QSharedPointer<iris::SceneNode> sceneNode);

protected:
    bool eventFilter(QObject *watched, QEvent *event);

protected slots:
    void treeItemSelected(QTreeWidgetItem* item);
    void treeItemChanged(QTreeWidgetItem* item,int index);
    void sceneTreeCustomContextMenu(const QPoint &);

    void renameNode();
    void deleteNode();
    void duplicateNode();

private:
    void repopulateTree();
    void populateTree(QTreeWidgetItem* parentNode,QSharedPointer<iris::SceneNode> sceneNode);

    // maps scene nodes to their widgetitems
    QMap<long, QSharedPointer<iris::SceneNode>> nodeList;
    QMap<long, QTreeWidgetItem*> treeItemList;

    QSharedPointer<iris::SceneNode> lastDraggedHiearchyItemSrc;

private:
    Ui::SceneHeirarchyWidget *ui;
    QSharedPointer<iris::Scene> scene;
    QSharedPointer<iris::SceneNode> selectedNode;
    MainWindow* mainWindow;

signals:
    void sceneNodeSelected(iris::SceneNodePtr sceneNode);
};

#endif // SCENEHEIRARCHYWIDGET_H
