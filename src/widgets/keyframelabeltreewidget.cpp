/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "keyframelabeltreewidget.h"
#include "ui_keyframelabeltreewidget.h"

#include <QStringList>
#include <QTreeWidget>
#include <QMenu>
#include "../irisgl/src/animation/keyframeanimation.h"
#include "../irisgl/src/animation/keyframeset.h"
#include "../irisgl/src/animation/animation.h"
#include "../irisgl/src/animation/propertyanim.h"
#include "../irisgl/src/scenegraph/scenenode.h"

#include "animationwidget.h"


class KeyFrameGroup
{
public:
    QString nameSpace;
    QList<QPair<QString,iris::FloatKeyFrame*>> frames;
};

KeyFrameLabelTreeWidget::KeyFrameLabelTreeWidget(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::KeyFrameLabelTreeWidget)
{
    ui->setupUi(this);

    connect(ui->treeWidget, SIGNAL(itemCollapsed(QTreeWidgetItem*)), this, SLOT(itemCollapsed(QTreeWidgetItem*)));
    connect(ui->treeWidget, SIGNAL(itemExpanded(QTreeWidgetItem*)), this, SLOT(itemExpanded(QTreeWidgetItem*)));
    connect(ui->treeWidget, SIGNAL(customContextMenuRequested(QPoint)), this, SLOT(customContextMenuRequested(QPoint)));

    ui->treeWidget->setContextMenuPolicy(Qt::CustomContextMenu);
    rightClickedItem = nullptr;
}

KeyFrameLabelTreeWidget::~KeyFrameLabelTreeWidget()
{
    delete ui;
}

void KeyFrameLabelTreeWidget::setSceneNode(iris::SceneNodePtr node)
{
    this->node = node;

    if (!node) {
        setActiveAnimation(iris::AnimationPtr());
        return;
    }

    // refresh tree
    auto animation = node->getAnimation();
    setActiveAnimation(animation);
}

void KeyFrameLabelTreeWidget::setActiveAnimation(iris::AnimationPtr animation)
{
    ui->treeWidget->clear();
    if (!!animation) {
        for(auto prop : animation->properties) {
            // add prop to tree
            //switch(prop->)
            addPropertyToTree(prop);
        }
    }
}

void KeyFrameLabelTreeWidget::highlightDefaultProperty()
{
    if (ui->treeWidget->selectedItems().size() == 0) {
        auto root = ui->treeWidget->invisibleRootItem();
        if (root->childCount()!=0)
            root->child(0)->setSelected(true);
    }
}

void KeyFrameLabelTreeWidget::addProperty(QString propName)
{
    auto prop = node->getAnimation()->properties[propName];
    addPropertyToTree(prop);
}

QTreeWidget *KeyFrameLabelTreeWidget::getTree()
{
    return ui->treeWidget;
}

void KeyFrameLabelTreeWidget::addPropertyToTree(iris::PropertyAnim *prop)
{
    auto frames = prop->getKeyFrames();
    auto treeItem = new QTreeWidgetItem();
    treeItem->setText(0,prop->getName());

    if (frames.size() == 1) {
        KeyFrameData frameData;
        frameData.keyFrame = frames[0].keyFrame;
        frameData.propertyName = prop->getName();
        frameData.subPropertyName = "";

        treeItem->setData(0,Qt::UserRole,QVariant::fromValue(frameData));
    } else {

        KeyFrameData frameData;
        //frameData.keyFrame = frames[0].keyFrame;
        frameData.keyFrame = nullptr;
        frameData.propertyName = prop->getName();
        frameData.subPropertyName = "";
        calculateSummaryKeys(prop, frameData);
        treeItem->setData(0,Qt::UserRole,QVariant::fromValue(frameData));

        for (auto frame : frames) {
            auto childItem = new QTreeWidgetItem();
            childItem->setText(0,frame.name);

            KeyFrameData frameData;
            frameData.keyFrame = frame.keyFrame;
            frameData.propertyName = prop->getName();
            frameData.subPropertyName = frame.name;

            childItem->setData(0, Qt::UserRole, QVariant::fromValue(frameData));
            treeItem->addChild(childItem);
        }
    }

    ui->treeWidget->invisibleRootItem()->addChild(treeItem);
}

void KeyFrameLabelTreeWidget::itemCollapsed(QTreeWidgetItem *item)
{
    animWidget->repaintViews();
}

void KeyFrameLabelTreeWidget::itemExpanded(QTreeWidgetItem *item)
{
    animWidget->repaintViews();
}

void KeyFrameLabelTreeWidget::customContextMenuRequested(const QPoint &point)
{
    QMenu* menu = new QMenu();
    auto item = ui->treeWidget->itemAt(point);

    if (item == nullptr)
        return;

    rightClickedItem = item;

    // whole poperties can be deleted, subproperties can only be cleared
    KeyFrameData data = item->data(0,Qt::UserRole).value<KeyFrameData>();

    if(data.subPropertyName.isEmpty()) {
        // it's a property, thus it can be deleted
        auto action = new QAction("Remove Property");
        menu->addAction(action);
    }

    auto action = new QAction("Clear Keys");
    menu->addAction(action);
    connect(menu, SIGNAL(triggered(QAction*)), this, SLOT(onTreeItemContextMenu(QAction*)));

    menu->exec(this->mapToGlobal(point));


}

void KeyFrameLabelTreeWidget::onTreeItemContextMenu(QAction *action)
{
    KeyFrameData data = rightClickedItem->data(0,Qt::UserRole).value<KeyFrameData>();
    if(action->text()=="Remove Property") {
        animWidget->removeProperty(data.propertyName);
        //this->removeProperty(data.propertyName);
    } else if(action->text()=="Clear Keys") {

        if(data.subPropertyName.isEmpty()) {
            this->clearPropertyKeyFrame(data.propertyName);
        } else {
            this->clearSubPropertyKeyFrame(data.propertyName, data.subPropertyName);
        }
    }
}

void KeyFrameLabelTreeWidget::removeProperty(QString propertyName)
{
    //remove from tree
    for (int i=0; i<  ui->treeWidget->invisibleRootItem()->childCount(); i++) {

        auto item = ui->treeWidget->invisibleRootItem()->child(i);
        auto data = item->data(0, Qt::UserRole).value<KeyFrameData>();

        if (data.propertyName == propertyName) {
            ui->treeWidget->invisibleRootItem()->removeChild(item);
            break;
        }
    }
}

void KeyFrameLabelTreeWidget::clearPropertyKeyFrame(QString propertyName)
{
    animWidget->clearPropertyKeys(propertyName);
}

void KeyFrameLabelTreeWidget::clearSubPropertyKeyFrame(QString propertyName, QString subPropertyName)
{

}

QTreeWidgetItem *KeyFrameLabelTreeWidget::getSelectedTreeItem()
{
    auto items = ui->treeWidget->selectedItems();
    if (items.size() == 0)
        return nullptr;

    return items[0];
}

void KeyFrameLabelTreeWidget::calculateSummaryKeys(iris::PropertyAnim *prop, KeyFrameData &keyFrameData)
{
    // start fresh
    keyFrameData.summaryKeys.clear();

    auto frames = prop->getKeyFrames();

    for ( auto frame : frames) {
        auto keyFrame = frame.keyFrame;
        for( auto key : keyFrame->keys) {
            // add key to keyframe
            auto& summaryKeys = keyFrameData.summaryKeys;
            if (!summaryKeys.contains(key->time)) {
                SummaryKey sumKey;
                sumKey.propertyName = prop->getName();
                sumKey.keys.append(key);
                summaryKeys.insert(key->time, sumKey);
            } else {
                summaryKeys[key->time].keys.append(key);
            }
        }
    }
}

void KeyFrameLabelTreeWidget::recalcPropertySummaryKeys(QString name)
{
    auto prop = node->getAnimation()->getPropertyAnim(name);
    auto frameData = getPropertyKeyFrameData(name);

    calculateSummaryKeys(prop, frameData);
    setPropertyKeyFrameData(name, frameData);
}

void KeyFrameLabelTreeWidget::setAnimWidget(AnimationWidget *value)
{
    animWidget = value;
}

KeyFrameData KeyFrameLabelTreeWidget::getPropertyKeyFrameData(QString propName)
{
    for (int i=0; i<  ui->treeWidget->invisibleRootItem()->childCount(); i++) {

        auto item = ui->treeWidget->invisibleRootItem()->child(i);
        auto data = item->data(0, Qt::UserRole).value<KeyFrameData>();

        if (data.propertyName == propName) {
            return data;
        }
    }

    // shouldnt be here
    Q_ASSERT(false);
}

void KeyFrameLabelTreeWidget::setPropertyKeyFrameData(QString propName, KeyFrameData keyFrameData)
{
    for (int i=0; i<  ui->treeWidget->invisibleRootItem()->childCount(); i++) {

        auto item = ui->treeWidget->invisibleRootItem()->child(i);
        auto data = item->data(0, Qt::UserRole).value<KeyFrameData>();

        if (data.propertyName == propName) {
            item->setData(0, Qt::UserRole, QVariant::fromValue(keyFrameData));
        }
    }
}


float SummaryKey::getTime()
{
    return keys[0]->time;
}
