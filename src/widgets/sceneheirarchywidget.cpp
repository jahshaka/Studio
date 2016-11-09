#include "sceneheirarchywidget.h"
#include "ui_sceneheirarchywidget.h"

#include <QTreeWidgetItem>

#include "../jah3d/core/scene.h"
#include "../jah3d/core/scenenode.h"

SceneHeirarchyWidget::SceneHeirarchyWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SceneHeirarchyWidget)
{
    ui->setupUi(this);

    connect(ui->sceneTree,SIGNAL(itemClicked(QTreeWidgetItem*,int)),this,SLOT(sceneNodeSelected(QTreeWidgetItem*)));
    connect(ui->sceneTree,SIGNAL(itemChanged(QTreeWidgetItem*,int)),this,SLOT(sceneTreeItemChanged(QTreeWidgetItem*,int)));

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

void SceneHeirarchyWidget::setScene(QSharedPointer<jah3d::Scene> scene)
{

}

void SceneHeirarchyWidget::sceneNodeSelected(QTreeWidgetItem* item)
{

}

void SceneHeirarchyWidget::repopulateTree()
{
    auto sceneRoot = scene->getRootNode();
    auto root = new QTreeWidgetItem();

    root->setText(0,sceneRoot->getName());
    //root->setIcon(0,this->getIconFromSceneNodeType(SceneNodeType::World));
    //root->setData(1,Qt::UserRole,QVariant::fromValue((void*)sceneRoot));

    //populate tree
    //sceneTreeItems.clear();
    populateTree(root,sceneRoot);

    ui->sceneTree->clear();
    ui->sceneTree->addTopLevelItem(root);
    ui->sceneTree->expandAll();
}

void SceneHeirarchyWidget::populateTree(QTreeWidgetItem* parentNode,QSharedPointer<jah3d::SceneNode> sceneNode)
{
    for(auto node:sceneNode->children)
    {

        auto childNode = new QTreeWidgetItem();
        childNode->setText(0,node->getName());
        //childNode->setData(1,Qt::UserRole,QVariant::fromValue((void*)node));
        //childNode->setIcon(0,this->getIconFromSceneNodeType(node->sceneNodeType));
        childNode->setFlags(childNode->flags() | Qt::ItemIsUserCheckable);
        childNode->setCheckState(0,Qt::Checked);


        //if(!node->isVisible())
        //    childNode->setCheckState(0,Qt::Unchecked);

        parentNode->addChild(childNode);

        //sceneTreeItems.insert(node->getEntity()->id(),childNode);

        populateTree(childNode,node);
    }
}

void SceneHeirarchyWidget::sceneTreeItemChanged(QTreeWidgetItem* item,int index)
{

}

SceneHeirarchyWidget::~SceneHeirarchyWidget()
{
    delete ui;
}
