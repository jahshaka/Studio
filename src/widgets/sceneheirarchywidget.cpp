/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "sceneheirarchywidget.h"
#include "ui_sceneheirarchywidget.h"

#include <QTreeWidgetItem>
#include <QMenu>
#include <QDebug>



#include "../irisgl/src/core/scene.h"
#include "../irisgl/src/core/scenenode.h"
#include "../mainwindow.h"

SceneHeirarchyWidget::SceneHeirarchyWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SceneHeirarchyWidget)
{
    ui->setupUi(this);

    mainWindow = nullptr;

    ui->sceneTree->viewport()->installEventFilter(this);

    connect(ui->sceneTree, SIGNAL(itemClicked(QTreeWidgetItem*, int)),
            this,
            SLOT(treeItemSelected(QTreeWidgetItem*)));

    connect(ui->sceneTree,
            SIGNAL(itemChanged(QTreeWidgetItem*, int)),
            this,
            SLOT(treeItemChanged(QTreeWidgetItem*, int)));

    //make items draggable and droppable
    ui->sceneTree->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->sceneTree->setDragEnabled(true);
    ui->sceneTree->viewport()->setAcceptDrops(true);
    ui->sceneTree->setDropIndicatorShown(true);
    ui->sceneTree->setDragDropMode(QAbstractItemView::InternalMove);

    //custom context menu
    //http://stackoverflow.com/questions/22198427/adding-a-right-click-menu-for-specific-items-in-qtreeview
    ui->sceneTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->sceneTree, SIGNAL(customContextMenuRequested(const QPoint &)), this, SLOT(sceneTreeCustomContextMenu(const QPoint &)));

}

void SceneHeirarchyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    //todo: cleanly remove previous scene
    this->scene = scene;
    this->repopulateTree();
}

void SceneHeirarchyWidget::setMainWindow(MainWindow* mainWin)
{
    mainWindow = mainWin;

    //ADD BUTTON
    //todo: bind callbacks
    QMenu* addMenu = new QMenu();

    auto primtiveMenu = addMenu->addMenu("Primitive");
    QAction *action = new QAction("Torus", this);
    primtiveMenu->addAction(action);
    connect(action,SIGNAL(triggered()),mainWindow,SLOT(addTorus()));

    action = new QAction("Cube", this);
    primtiveMenu->addAction(action);
    connect(action,SIGNAL(triggered()),mainWindow,SLOT(addCube()));

    action = new QAction("Sphere", this);
    primtiveMenu->addAction(action);
    connect(action,SIGNAL(triggered()),mainWindow,SLOT(addSphere()));

    action = new QAction("Cylinder", this);
    primtiveMenu->addAction(action);
    connect(action,SIGNAL(triggered()),mainWindow,SLOT(addCylinder()));

    action = new QAction("Plane", this);
    primtiveMenu->addAction(action);
    connect(action,SIGNAL(triggered()),mainWindow,SLOT(addPlane()));

    //LIGHTS
    auto lightMenu = addMenu->addMenu("Light");
    action = new QAction("Point", this);
    lightMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addPointLight()));

    action = new QAction("Spot", this);
    lightMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addSpotLight()));

    action = new QAction("Directional", this);
    lightMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addDirectionalLight()));

    action = new QAction("Empty", this);
    addMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addEmpty()));

    action = new QAction("Viewer", this);
    addMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addViewer()));

    //SYSTEMS
    action = new QAction("Particle System", this);
    addMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addParticleSystem()));

    //MESHES
    action = new QAction("Load Model", this);
    addMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addMesh()));

    //VIEWPOINT
    /*
    action = new QAction("ViewPoint", this);
    addMenu->addAction(action);
    connect(action,SIGNAL(triggered()),mainWindow,SLOT(addViewPoint()));
    */

    ui->addBtn->setMenu(addMenu);
    ui->addBtn->setPopupMode(QToolButton::InstantPopup);

    connect(ui->deleteBtn, SIGNAL(clicked(bool)), mainWindow, SLOT(deleteNode()));
}

void SceneHeirarchyWidget::setSelectedNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    selectedNode = sceneNode;

    //set tree item
    //todo: check if item exists in nodeList
    //auto selected = ui->sceneTree->selectedItems();
    //for(auto item:selected)
    //    item->setSelected(false);

    if(!!sceneNode) {
        auto item = treeItemList[sceneNode->getNodeId()];
        //item->setSelected(true);
        ui->sceneTree->setCurrentItem(item);
    }
}

bool SceneHeirarchyWidget::eventFilter(QObject *watched, QEvent *event)
{
    // @TODO, handle multiple items later on
    if (event->type() == QEvent::Drop) {
        auto dropEventPtr = static_cast<QDropEvent*>(event);
        this->dropEvent(dropEventPtr);

        QTreeWidgetItem *droppedIndex = ui->sceneTree->itemAt(dropEventPtr->pos().x(),
                                                              dropEventPtr->pos().y());
        if (droppedIndex) {
            long itemId = droppedIndex->data(1, Qt::UserRole).toLongLong();
            auto source = nodeList[itemId];
            source->addChild(this->lastDraggedHiearchyItemSrc);
        }
    }

    if (event->type() == QEvent::DragEnter) {
        auto eventPtr = static_cast<QDragEnterEvent*>(event);

        auto selected = ui->sceneTree->selectedItems();
        if (selected.size() > 0) {
            long itemId = selected[0]->data(1, Qt::UserRole).toLongLong();

            auto source = nodeList[itemId];
            lastDraggedHiearchyItemSrc = source;
        }
    }

    return QObject::eventFilter(watched, event);
}

void SceneHeirarchyWidget::treeItemSelected(QTreeWidgetItem* item)
{
    long nodeId = item->data(1, Qt::UserRole).toLongLong();
    selectedNode = nodeList[nodeId];

    emit sceneNodeSelected(selectedNode);
}

void SceneHeirarchyWidget::treeItemChanged(QTreeWidgetItem* item, int column)
{
    long nodeId = item->data(1,Qt::UserRole).toLongLong();
    auto node = nodeList[nodeId];

    if (item->checkState(column) == Qt::Checked) {
        node->show();
    } else {
        node->hide();
    }
}

void SceneHeirarchyWidget::sceneTreeCustomContextMenu(const QPoint& pos)
{
    QModelIndex index = ui->sceneTree->indexAt(pos);
    if (!index.isValid()) {
        return;
    }

    auto item = ui->sceneTree->itemAt(pos);
    auto nodeId = (long)item->data(1,Qt::UserRole).toLongLong();
    auto node = nodeList[nodeId];

    QMenu menu;
    QAction* action;

    //rename
    action = new QAction(QIcon(),"Rename",this);
    connect(action,SIGNAL(triggered()),this,SLOT(renameNode()));
    menu.addAction(action);

    //world node isnt removable
    if(node->isRemovable())
    {
        action = new QAction(QIcon(),"Delete",this);
        connect(action,SIGNAL(triggered()),this,SLOT(deleteNode()));
        menu.addAction(action);
    }

    if(node->isDuplicable())
    {
        action = new QAction(QIcon(),"Duplicate",this);
        connect(action,SIGNAL(triggered()),this,SLOT(duplicateNode()));
        menu.addAction(action);
    }

    selectedNode = node;
    menu.exec(ui->sceneTree->mapToGlobal(pos));
}

void SceneHeirarchyWidget::renameNode()
{
    //mainWindow->deleteNode();
    mainWindow->renameNode();
}

void SceneHeirarchyWidget::deleteNode()
{
    mainWindow->deleteNode();
    selectedNode.clear();
}

void SceneHeirarchyWidget::duplicateNode()
{
    mainWindow->duplicateNode();
}

void SceneHeirarchyWidget::repopulateTree()
{
    auto rootNode = scene->getRootNode();
    auto rootTreeItem = new QTreeWidgetItem();

    rootTreeItem->setText(0, rootNode->getName());
    //root->setIcon(0,this->getIconFromSceneNodeType(SceneNodeType::World));
    rootTreeItem->setData(1, Qt::UserRole,QVariant::fromValue(rootNode->getNodeId()));

    // populate tree
    nodeList.clear();
    treeItemList.clear();

    nodeList.insert(rootNode->getNodeId(), rootNode);
    treeItemList.insert(rootNode->getNodeId(), rootTreeItem);

    populateTree(rootTreeItem,rootNode);

    ui->sceneTree->clear();
    ui->sceneTree->addTopLevelItem(rootTreeItem);
    ui->sceneTree->expandAll();
}

void SceneHeirarchyWidget::populateTree(QTreeWidgetItem* parentTreeItem,
                                        QSharedPointer<iris::SceneNode> sceneNode)
{
    for (auto childNode : sceneNode->children) {
        auto childTreeItem = new QTreeWidgetItem();
        childTreeItem->setText(0, childNode->getName());
        childTreeItem->setData(1, Qt::UserRole,QVariant::fromValue(childNode->getNodeId()));
        // childNode->setIcon(0,this->getIconFromSceneNodeType(node->sceneNodeType));
        childTreeItem->setFlags(childTreeItem->flags() | Qt::ItemIsUserCheckable);
        childTreeItem->setCheckState(0, Qt::Checked);

        // if (!node->isVisible()) childNode->setCheckState(0,Qt::Unchecked);

        parentTreeItem->addChild(childTreeItem);

        // sceneTreeItems.insert(node->getEntity()->id(),childNode);
        nodeList.insert(childNode->getNodeId(), childNode);
        treeItemList.insert(childNode->getNodeId(), childTreeItem);

        populateTree(childTreeItem, childNode);
    }
}

SceneHeirarchyWidget::~SceneHeirarchyWidget()
{
    delete ui;
}
