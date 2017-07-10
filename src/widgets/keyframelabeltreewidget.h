/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef KEYFRAMELABELTREEWIDGET_H
#define KEYFRAMELABELTREEWIDGET_H

#include <QWidget>
#include <QHash>
#include "../irisgl/src/irisglfwd.h"

namespace Ui {
class KeyFrameLabelTreeWidget;
}

class KeyFrameGroup;
class QTreeWidget;
class QTreeWidgetItem;
class AnimationWidget;

struct KeyFrameData
{
    QString propertyName;
    QString subPropertyName;
    iris::FloatKeyFrame* keyFrame;
    //iris::PropertyAnim *prop;

    bool isSubProperty()
    {
        return !subPropertyName.isEmpty();
    }

    bool isProperty()
    {
        return subPropertyName.isEmpty();
    }
};

Q_DECLARE_METATYPE(KeyFrameData)

class KeyFrameLabelTreeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KeyFrameLabelTreeWidget(QWidget *parent = 0);
    ~KeyFrameLabelTreeWidget();

    void setSceneNode(iris::SceneNodePtr node);
    void setActiveAnimation(iris::AnimationPtr anim);

    // propName must be a name
    void addProperty(QString propName);

    QTreeWidget* getTree();

    void setAnimWidget(AnimationWidget *value);

private:
    void addPropertyToTree(iris::PropertyAnim* prop);

public slots:
    void itemCollapsed(QTreeWidgetItem* item);
    void itemExpanded(QTreeWidgetItem* item);

    void customContextMenuRequested(const QPoint&);

    void onTreeItemContextMenu(QAction* action);

    void removeProperty(QString propertyName);
    void clearPropertyKeyFrame(QString propertyName);
    void clearSubPropertyKeyFrame(QString propertyName, QString subPropertyName);

    QTreeWidgetItem* getSelectedTreeItem();
private:
    Ui::KeyFrameLabelTreeWidget *ui;
    iris::SceneNodePtr node;
    QHash<QString,KeyFrameGroup*> groups;
    AnimationWidget* animWidget;

    QTreeWidgetItem* rightClickedItem;

    // context menu actions
    QAction* removePropertyAction;
    QAction* clearKeyFrameAction;

    void parseKeyFramesToGroups(iris::KeyFrameSetPtr frameSet);

    void buildTreeFromKeyFrameGroups(QHash<QString,KeyFrameGroup*> groups);
};

#endif // KEYFRAMELABELTREEWIDGET_H
