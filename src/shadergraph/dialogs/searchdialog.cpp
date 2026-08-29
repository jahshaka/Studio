/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "searchdialog.h"
#include <QVBoxLayout>
#include <QLineEdit>
#include <QTabWidget>
#include <QKeyEvent>
#include <QGraphicsEffect>
#include <QDebug>

#include "../models/libraryv1.h"
#include "../models/properties.h"
#include "src/core/project.h"



SearchDialog::SearchDialog(NodeGraph *graph, GraphNodeScene* scene, QPoint point) : QDialog()
{
	setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::X11BypassWindowManagerHint);
	setAttribute(Qt::WA_TranslucentBackground);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setMouseTracking(true);
	setWindowFlag(Qt::SubWindow);
	setAttribute(Qt::WA_QuitOnClose, false);
	

	auto widgetHolder = new QWidget;
	auto widgetLayout = new QVBoxLayout;
	widgetHolder->setLayout(widgetLayout);
	widgetLayout->setContentsMargins(5,5,5,5);

	auto clearWidget = new QWidget;
	auto clearLayout = new QVBoxLayout;
	clearWidget->setLayout(clearLayout);
	clearLayout->setContentsMargins(25, 25, 25, 25);
	clearWidget->setObjectName(QStringLiteral("clearwidget"));
	clearWidget->setStyleSheet("QWidget#clearwidget{background : rgba(0,0,0,0);}");

	QGraphicsDropShadowEffect *effect = new QGraphicsDropShadowEffect;
	effect->setBlurRadius(15);
	effect->setXOffset(0);
	effect->setYOffset(0);
	effect->setColor(QColor(0, 0, 0, 255));
	widgetHolder->setGraphicsEffect(effect);

	auto layout = new QVBoxLayout;
	setLayout(layout);
	setFixedSize(280, 450);
	setWindowModality(Qt::NonModal);

	auto tabWidget = new QTabWidget;
	auto nodeWidget = new QWidget;
	auto nodeLayout = new QVBoxLayout;

	//search box
	auto searchContainer = new QWidget;
	auto searchLayout = new QHBoxLayout;
	searchBar = new QLineEdit;
	searchLayout->setContentsMargins(0, 10, 0, 0);

	tree = new TreeWidget;
	tree->setContentsMargins(10, 10, 60, 10);
	treeProperty = new TreeWidget;
	configureTreeWidget();

	searchContainer->setLayout(searchLayout);
	searchLayout->addWidget(searchBar);
	searchLayout->addSpacing(0);

	searchBar->setPlaceholderText("search");
	searchBar->setAlignment(Qt::AlignLeft);
	searchBar->setTextMargins(6, 0, 0, 0);
	QSize currentSize(90, 90);

	nodeWidget->setLayout(nodeLayout);
	nodeLayout->addWidget(searchContainer);
	nodeLayout->addWidget(tree);

	tabWidget->addTab(nodeWidget, "Nodes");
	tabWidget->addTab(treeProperty, "Properties");


	tabWidget->setStyleSheet(
		"QTabWidget::pane{	border: 1px solid rgba(0, 0, 0, .1); border-top: 0px solid rgba(0, 0, 0, 0); padding-top: 7px; }"
		"QTabBar::tab{	background: rgba(21, 21, 21, .7); color: rgba(250, 250, 250, .9); font - weight: 400; font-size: 13em; padding: 5px 22px 5px 22px; }"
		"QTabBar::tab:selected{ color: rgba(255, 255, 255, .99); border-top: 2px solid rgba(50,150,250,.8); }"
		"QTabBar::tab:!selected{ background: rgba(55, 55, 55, .99); border : 1px solid rgba(21,21,21,.4); color: rgba(200,200,200,.5); }"
	);

	searchBar->setStyleSheet("border-radius : 2px; ");


	clearLayout->addWidget(widgetHolder);
	widgetLayout->addWidget(tabWidget);
	layout->addWidget(clearWidget);

	generateTileNode(graph);
	generateTileProperty(graph);


	connect(searchBar, &QLineEdit::textChanged, [=](QString str) {
		tree->clear();
		QList<NodeLibraryItem*> lis;
		for (auto item : graph->library->items) {
			if (item->displayName.contains(str, Qt::CaseInsensitive)) lis.append(item);
		}
		
		generateTileNode(lis);

		if (str.length() == 0) {
			tree->clear();
			configureTreeWidget();
			generateTileNode(graph);
		}

		auto item = tree->itemBelow(tree->topLevelItem(0));
		if (item) {
			item->setSelected(true);
			tree->setCurrentItem(item);
		}
	});

	connect(searchBar, &QLineEdit::returnPressed, [=]() {
		scene->addNodeFromSearchDialog(tree->currentItem(), this->point);
		this->close();
	});

	connect(tree, &TreeWidget::itemClicked, [=](QTreeWidgetItem *item, int column) {

		if (tree->indexOfTopLevelItem(item) == -1) { // is not top level item
			qDebug() << point;
			scene->addNodeFromSearchDialog(tree->currentItem(), this->point);
			this->close();
		}
		else { // is top level item 
			item->setExpanded(true);
		}
	});

	connect(treeProperty, &TreeWidget::itemClicked, [=](QTreeWidgetItem *item, int column) {
			scene->addNodeFromSearchDialog(treeProperty->currentItem(), this->point);
			this->close();		
	});


	searchContainer->setStyleSheet("background:rgba(32,32,32,0);");
	searchBar->setStyleSheet("QLineEdit{ background:rgba(41,41,41,1); border: 1px solid rgba(150,150,150,.2); border-radius: 1px; color: rgba(250,250,250,.95); padding: 6px;  }");

	setStyleSheet(""
		"QWidget{background: rgba(21,21,21,1); border: 0px solid rgba(0,0,0,0);}"
		"QListView::item{color: rgba(255,255,255,1); border-radius: 2px; border: 1px solid rgba(0,0,0,.31); background: rgba(51,51,51,1); margin: 3px;  }"
		"QListView::item:selected{ background: rgba(155,155,155,1); border: 1px solid rgba(50,150,250,.1); }"
		"QListView::item:hover{ background: rgba(95,95,95,1); border: .1px solid rgba(50,150,250,.1); }"
		"QListView::text{ top : -6; }"
	);


	if (point == QPoint(0,0)) {
		auto view = scene->views().first();
		auto viewPoint = view->viewport()->mapToGlobal(point);
		auto scenePoint = view->mapFromScene(viewPoint);
		this->point = scenePoint;
	}else 	this->point = point;

	//move view under mouse
	QPoint p(45,45);
	this->point = this->point - p;

	installEventFilter(this);

}


SearchDialog::~SearchDialog()
{
}

void SearchDialog::generateTileNode(QList<NodeLibraryItem*> lis)
{
	configureTreeWidget();
	QSize currentSize(20, 20);

	for (auto tile : lis) {

		auto item = new QTreeWidgetItem;
		item->setText(0,tile->displayName);
		item->setData(0,Qt::DisplayRole, tile->displayName);
		item->setData(0,Qt::UserRole, tile->name);
		item->setData(0,MODEL_TYPE_ROLE, "node");
		item->setSizeHint(0, currentSize);
		item->setTextAlignment(0,Qt::AlignLeft);
		item->setFlags(item->flags() | Qt::ItemIsEditable);
		tree->findItems(NodeModel::getEnumString(tile->nodeCategory), Qt::MatchExactly)[0]->addChild(item);
	}

	for (int i = tree->topLevelItemCount() - 1; i >= 0; i--) {
		auto item = tree->topLevelItem(i);
		if (item->childCount() < 1) {
			tree->takeTopLevelItem(i);
		}
	}
	for (int i = tree->topLevelItemCount() - 1; i >= 0; i--) {
		auto item = tree->topLevelItem(i);
		if (item) item->setExpanded(true);
	}
}

void SearchDialog::generateTileNode(NodeGraph *graph)
{

	QSize currentSize(20, 20);

	for (NodeLibraryItem *tile : graph->library->items) {
		if (tile->name == "property") continue;
		auto item = new QTreeWidgetItem;
		item->setText(0, tile->displayName);
		item->setData(0, Qt::DisplayRole, tile->displayName);
		item->setData(0, Qt::UserRole, tile->name);
		item->setData(0, MODEL_TYPE_ROLE, "node");
		item->setSizeHint(0, currentSize);
		item->setTextAlignment(0, Qt::AlignLeft);
		item->setFlags(item->flags() | Qt::ItemIsEditable);
		tree->findItems(NodeModel::getEnumString(tile->nodeCategory), Qt::MatchExactly)[0]->addChild(item);
	}
}

void SearchDialog::generateTileProperty(NodeGraph * graph)
{
	QSize currentSize(20, 20);

	for (auto tile : graph->properties) {
		if (tile->name == "property") continue;
		auto item = new QTreeWidgetItem;
		item->setText(0,tile->displayName);
		item->setData(0,Qt::DisplayRole, tile->displayName);
		item->setData(0,Qt::UserRole, tile->name);
		item->setData(0,MODEL_EXT_ROLE, index);
		item->setSizeHint(0,currentSize);
		item->setTextAlignment(0,Qt::AlignLeft);
		item->setFlags(item->flags() | Qt::ItemIsEditable);
		treeProperty->addTopLevelItem(item);
		index++;
	}

}

void SearchDialog::configureTreeWidget()
{
	for (int i = 0; i < (int)NodeCategory::PlaceHolder; i++) {
		auto wid = new QTreeWidgetItem;
		wid->setText(0, NodeModel::getEnumString(static_cast<NodeCategory>(i)));
		tree->addTopLevelItem(wid);
	}
}

void SearchDialog::leaveEvent(QEvent * event)
{
	this->close();
}

void SearchDialog::showEvent(QShowEvent * event)
{
	QDialog::showEvent(event);
	this->setGeometry(point.x(), point.y(), geometry().width(), geometry().height());
	searchBar->setFocus();
}

bool SearchDialog::eventFilter(QObject *watched, QEvent *e)
{
	if (watched == this) {
		auto keyEvent = static_cast<QKeyEvent*>(e);

		if (keyEvent->key() == Qt::Key_Up || keyEvent->key() == Qt::Key_Down) {
			tree->grabKeyboard();
		}
		else {
			searchBar->grabKeyboard();
			searchBar->releaseKeyboard();
		}
		return QDialog::eventFilter(watched, e);
	}
	return false;
}

