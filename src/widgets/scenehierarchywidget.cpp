/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "scenehierarchywidget.h"
#include "ui_scenehierarchywidget.h"

#include <QMenu>
#include <QTreeWidgetItem>

#include "irisgl/src/scenegraph/scene.h"
#include "irisgl/src/scenegraph/scenenode.h"
#include "irisgl/src/core/irisutils.h"
#include "mainwindow.h"
#include "uimanager.h"
#include "widgets/sceneviewwidget.h"
#include "io/scenewriter.h"

//#include <QProxyStyle>
//
//class MyProxyStyle : public QProxyStyle
//{
//public:
//	virtual void drawPrimitive(PrimitiveElement element, const QStyleOption * option,
//		QPainter * painter, const QWidget * widget = 0) const
//	{
//		if (PE_FrameFocusRect == element) {
//			// do not draw focus rectangle
//		} else {
//			QProxyStyle::drawPrimitive(element, option, painter, widget);
//		}
//	}
//};

SceneHierarchyWidget::SceneHierarchyWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SceneHierarchyWidget)
{
    ui->setupUi(this);

    mainWindow = nullptr;

	ui->sceneTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
	ui->sceneTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    ui->sceneTree->viewport()->installEventFilter(this);

    ui->sceneTree->setAttribute(Qt::WA_MacShowFocusRect, false);
    ui->sceneTree->viewport()->setAttribute(Qt::WA_MacShowFocusRect, false);

	connect(ui->sceneTree,	SIGNAL(itemClicked(QTreeWidgetItem*, int)),
			this,			SLOT(treeItemSelected(QTreeWidgetItem*, int)));

    // Make items draggable and droppable
    ui->sceneTree->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->sceneTree->setDragEnabled(true);
    ui->sceneTree->viewport()->setAcceptDrops(true);
    ui->sceneTree->setDropIndicatorShown(false);
	ui->sceneTree->setSelectionBehavior(QAbstractItemView::SelectRows);
    ui->sceneTree->setDragDropMode(QAbstractItemView::InternalMove);

    ui->sceneTree->setContextMenuPolicy(Qt::CustomContextMenu);

    connect(ui->sceneTree,	SIGNAL(customContextMenuRequested(const QPoint&)),
			this,			SLOT(sceneTreeCustomContextMenu(const QPoint&)));

	// We do QIcon::Selected manually to remove an annoying default highlight for selected icons
	visibleIcon = new QIcon;
	visibleIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-eye-48.png"), QIcon::Normal);
	visibleIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-eye-48.png"), QIcon::Selected);

	hiddenIcon = new QIcon;
	hiddenIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-eye-48-dim.png"), QIcon::Normal);
	hiddenIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-eye-48-dim.png"), QIcon::Selected);

	ui->sceneTree->setStyleSheet(
		"QTreeView, QTreeWidget { show-decoration-selected: 1; }"
		"QTreeWidget { outline: none; selection-background-color: #404040; color: #EEE; }"
		"QTreeWidget::branch { background-color: #202020; }"
		"QTreeWidget::branch:hover { background-color: #303030; }"
        "QTreeView::branch:open { image: url(:/icons/expand_arrow_open.png); }"
        "QTreeView::branch:closed:has-children { image: url(:/icons/expand_arrow_closed.png); }"
		"QTreeWidget::branch:selected { background-color: #404040; }"
		"QTreeWidget::item:selected { selection-background-color: #404040; background: #404040; outline: none; padding: 5px 0; }"
		/* Important, this is set for when the widget loses focus to fill the left gap */
		"QTreeWidget::item:selected:!active { background: #404040; padding: 5px 0; color: #EEE; }"
		"QTreeWidget::item:selected:active { background: #404040; padding: 5px 0; }"
		"QTreeWidget::item { padding: 5px 0; }"
		"QTreeWidget::item:hover { background: #303030; padding: 5px 0; }"
	);
}

void SceneHierarchyWidget::setScene(QSharedPointer<iris::Scene> scene)
{
    //todo: cleanly remove previous scene
    this->scene = scene;
    this->repopulateTree();
}

void SceneHierarchyWidget::setMainWindow(MainWindow *mainWin)
{
    mainWindow = mainWin;

    QMenu* addMenu = new QMenu();
	addMenu->setStyleSheet(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
		"QMenu::item : disabled { color: #555; }"
	);

	// Primitives
    auto primtiveMenu = addMenu->addMenu("Primitive");

    QAction *action = new QAction("Torus", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addTorus()));

    action = new QAction("Cube", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addCube()));

    action = new QAction("Sphere", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addSphere()));

    action = new QAction("Cylinder", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addCylinder()));

    action = new QAction("Plane", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addPlane()));

    action = new QAction("Ground", this);
    primtiveMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addGround()));

    // Lamps
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

    // Systems
    action = new QAction("Particle System", this);
    addMenu->addAction(action);
    connect(action, SIGNAL(triggered()), mainWindow, SLOT(addParticleSystem()));

    ui->addBtn->setMenu(addMenu);
    ui->addBtn->setPopupMode(QToolButton::InstantPopup);

    connect(ui->deleteBtn, SIGNAL(clicked(bool)), mainWindow, SLOT(deleteNode()));
}

void SceneHierarchyWidget::setSelectedNode(QSharedPointer<iris::SceneNode> sceneNode)
{
    selectedNode = sceneNode;

    if (!!sceneNode) {
        auto item = treeItemList[sceneNode->getNodeId()];
        ui->sceneTree->setCurrentItem(item);
    }
}

bool SceneHierarchyWidget::eventFilter(QObject *watched, QEvent *event)
{
    // @TODO, handle multiple items later on
    if (event->type() == QEvent::Drop) {
        auto dropEventPtr = static_cast<QDropEvent*>(event);
        this->dropEvent(dropEventPtr);

        QTreeWidgetItem *droppedIndex = ui->sceneTree->itemAt(dropEventPtr->pos().x(), dropEventPtr->pos().y());
        if (droppedIndex) {
            long itemId = droppedIndex->data(0, Qt::UserRole).toLongLong();
            auto source = nodeList[itemId];
            source->addChild(this->lastDraggedHiearchyItemSrc);
        }
    }

	if (event->type() == QEvent::DragMove) {
		auto evt = static_cast<QDragMoveEvent*>(event);
		QTreeWidgetItem* item = ui->sceneTree->itemAt(evt->pos());
		//if (ui->sceneTree->currentColumn() != 1) qDebug() << "yes";
	}

    if (event->type() == QEvent::DragEnter) {
        auto evt = static_cast<QDragEnterEvent*>(event);

        auto selected = ui->sceneTree->selectedItems();
        if (selected.size() > 0) {
            long itemId = selected[0]->data(0, Qt::UserRole).toLongLong();
            auto source = nodeList[itemId];
            lastDraggedHiearchyItemSrc = source;
        }
    }

    return QObject::eventFilter(watched, event);
}

void SceneHierarchyWidget::treeItemSelected(QTreeWidgetItem *item, int column)
{
    long nodeId = item->data(0, Qt::UserRole).toLongLong();

	// Our icons are in the second column
	if (column == 1) {
		auto node = nodeList[nodeId];
		if (item->data(1, Qt::UserRole).toBool()) {
			item->setIcon(1, *hiddenIcon);
			node->hide();
			item->setData(1, Qt::UserRole, QVariant::fromValue(false));
		}
		else {
			item->setIcon(1, *visibleIcon);
			node->show();
			item->setData(1, Qt::UserRole, QVariant::fromValue(true));
		}
	}
	else {
		selectedNode = nodeList[nodeId];
		emit sceneNodeSelected(selectedNode);
	}
}

void SceneHierarchyWidget::sceneTreeCustomContextMenu(const QPoint& pos)
{
    QModelIndex index = ui->sceneTree->indexAt(pos);
    if (!index.isValid()) return;

    auto item = ui->sceneTree->itemAt(pos);
    auto nodeId = (long) item->data(0, Qt::UserRole).toLongLong();
    auto node = nodeList[nodeId];

	selectedNode = node;

    QMenu menu;
	menu.setStyleSheet(
		"QMenu { background-color: #1A1A1A; color: #EEE; padding: 0; margin: 0; }"
		"QMenu::item { background-color: #1A1A1A; padding: 6px 8px; margin: 0; }"
		"QMenu::item:selected { background-color: #3498db; color: #EEE; padding: 6px 8px; margin: 0; }"
		"QMenu::item : disabled { color: #555; }"
	);

    QAction* action;

    action = new QAction(QIcon(), "Rename", this);
    connect(action, SIGNAL(triggered()), this, SLOT(renameNode()));
    menu.addAction(action);

    // The world node isn't removable
    if (node->isRemovable()) {
        action = new QAction(QIcon(), "Delete", this);
        connect(action, SIGNAL(triggered()), this, SLOT(deleteNode()));
        menu.addAction(action);
    }

    if (node->isDuplicable()) {
        action = new QAction(QIcon(), "Duplicate", this);
        connect(action, SIGNAL(triggered()), this, SLOT(duplicateNode()));
        menu.addAction(action);
    }

	action = new QAction(QIcon(), "Focus Camera", this);
	connect(action, SIGNAL(triggered()), this, SLOT(focusOnNode()));
	menu.addAction(action);

	if (node->isExportable()) {
		QMenu *subMenu = menu.addMenu("Export");

		std::function<void(const iris::SceneNodePtr&, QStringList&)> getChildGuids =
			[&](const iris::SceneNodePtr &node, QStringList &items) -> void
		{
			if (!node->getGUID().isEmpty() && !items.contains(node->getGUID())) items.append(node->getGUID());
			if (node->hasChildren()) {
				for (const auto &child : node->children) {
					getChildGuids(child, items);
				}
			}
		};

		if (node->getSceneNodeType() == iris::SceneNodeType::Empty) {
			QAction *exportAsset = subMenu->addAction("Export Object");

			QStringList assetGuids;
			getChildGuids(node, assetGuids);
			connect(exportAsset, &QAction::triggered, this, [assetGuids, this]() { mainWindow->exportNodes(assetGuids); });
		}

		if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
            if (!node->isBuiltIn) {
                QAction *exportAsset = subMenu->addAction("Export Object");
                connect(exportAsset, &QAction::triggered, this, [this, node]() {
                    exportNode(node);
                });
            }

			QAction *exportMat = subMenu->addAction("Create Material");
			connect(exportMat, &QAction::triggered, this, [this, node]() {
				createMaterial();
			});
		}
		else if (node->getSceneNodeType() == iris::SceneNodeType::ParticleSystem) {
			QAction *exportPSystem = subMenu->addAction("Export Particle System");

			connect(exportPSystem, &QAction::triggered, this, [this, node]() {
				exportParticleSystem(node);
			});
		}
	}

    menu.exec(ui->sceneTree->mapToGlobal(pos));
}

void SceneHierarchyWidget::renameNode()
{
    mainWindow->renameNode();
}

void SceneHierarchyWidget::deleteNode()
{
    mainWindow->deleteNode();
    selectedNode.clear();
}

void SceneHierarchyWidget::duplicateNode()
{
    mainWindow->duplicateNode();
}

void SceneHierarchyWidget::focusOnNode()
{
	UiManager::sceneViewWidget->focusOnNode(selectedNode);
}

void SceneHierarchyWidget::exportNode(const iris::SceneNodePtr &node)
{
	mainWindow->exportNode(node);
}

void SceneHierarchyWidget::createMaterial()
{
	mainWindow->createMaterial();
}

void SceneHierarchyWidget::exportParticleSystem(const iris::SceneNodePtr &node)
{
	mainWindow->exportNode(node);
}

void SceneHierarchyWidget::showHideNode(QTreeWidgetItem* item, bool show)
{
	long nodeId = item->data(1,Qt::UserRole).toLongLong();
    auto node = nodeList[nodeId];

    if (show) {
        node->show();
    } else {
        node->hide();
    }
}

void SceneHierarchyWidget::repopulateTree()
{
    auto rootNode = scene->getRootNode();
    auto rootTreeItem = new QTreeWidgetItem();

	QIcon *hiddenIcon = new QIcon;
	hiddenIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-globe-64.png"), QIcon::Normal);
	hiddenIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-globe-64.png"), QIcon::Selected);

    rootTreeItem->setText(0, rootNode->getName());
    rootTreeItem->setData(0, Qt::UserRole, QVariant::fromValue(rootNode->getNodeId()));
	rootTreeItem->setIcon(0, *hiddenIcon);

    // populate tree
    nodeList.clear();
    treeItemList.clear();

    nodeList.insert(rootNode->getNodeId(), rootNode);
    treeItemList.insert(rootNode->getNodeId(), rootTreeItem);

    populateTree(rootTreeItem, rootNode);

    ui->sceneTree->clear();
    ui->sceneTree->addTopLevelItem(rootTreeItem);
    ui->sceneTree->expandItem(rootTreeItem);
    //ui->sceneTree->expandAll();
}

void SceneHierarchyWidget::populateTree(QTreeWidgetItem* parentTreeItem,
                                        QSharedPointer<iris::SceneNode> sceneNode)
{
    for (auto childNode : sceneNode->children) {
        auto childTreeItem = createTreeItems(childNode);
        parentTreeItem->addChild(childTreeItem);
        nodeList.insert(childNode->getNodeId(), childNode);
        treeItemList.insert(childNode->getNodeId(), childTreeItem);
        populateTree(childTreeItem, childNode);
    }
}

QTreeWidgetItem *SceneHierarchyWidget::createTreeItems(iris::SceneNodePtr node)
{
    auto childTreeItem = new QTreeWidgetItem();
    childTreeItem->setText(0, node->getName());
    childTreeItem->setData(0, Qt::UserRole, QVariant::fromValue(node->getNodeId()));
	childTreeItem->setData(1, Qt::UserRole, QVariant::fromValue(node->isVisible()));

	QIcon *nodeIcon = new QIcon;
	
	if (node->getSceneNodeType() == iris::SceneNodeType::Mesh) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-mesh-32.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-mesh-32.png"), QIcon::Selected);
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::Light) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-sun-48.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-sun-48.png"), QIcon::Selected);
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::ParticleSystem) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-snow-storm-26.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-snow-storm-26.png"), QIcon::Selected);
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::Empty) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-average-math-filled-50.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-average-math-filled-50.png"), QIcon::Selected);
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::Viewer) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-virtual-reality-filled-50.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-virtual-reality-filled-50.png"), QIcon::Selected);
	}
	else if (node->getSceneNodeType() == iris::SceneNodeType::Camera) {
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-camera-48.png"), QIcon::Normal);
		nodeIcon->addPixmap(IrisUtils::getAbsoluteAssetPath("app/icons/icons8-camera-48.png"), QIcon::Selected);
	}

	childTreeItem->setIcon(0, *nodeIcon);
	
	node->isVisible() ? childTreeItem->setIcon(1, *visibleIcon) : childTreeItem->setIcon(1, *hiddenIcon);

    return childTreeItem;
}

void SceneHierarchyWidget::insertChild(iris::SceneNodePtr childNode)
{
    auto parentTreeItem = treeItemList[childNode->parent->nodeId];
    auto childItem = createTreeItems(childNode);
    parentTreeItem->insertChild(childNode->parent->children.indexOf(childNode),childItem);

    // add to lists
    nodeList.insert(childNode->getNodeId(), childNode);
    treeItemList.insert(childNode->getNodeId(), childItem);

    // recursively add children
    for (auto child: childNode->children) insertChild(child);
}

void SceneHierarchyWidget::removeChild(iris::SceneNodePtr childNode)
{
    // remove from heirarchy
    auto nodeTreeItem = treeItemList[childNode->nodeId];
    nodeTreeItem->parent()->removeChild(nodeTreeItem);

    // remove from lists
    nodeList.remove(childNode->getNodeId());
    treeItemList.remove(childNode->getNodeId());
}

SceneHierarchyWidget::~SceneHierarchyWidget()
{
    delete ui;
}
