#ifndef KEYFRAMELABELTREEWIDGET_H
#define KEYFRAMELABELTREEWIDGET_H

#include <QWidget>
#include <QHash>
#include "../irisglfwd.h"

namespace Ui {
class KeyFrameLabelTreeWidget;
}

class KeyFrameGroup;
class KeyFrameLabelTreeWidget : public QWidget
{
    Q_OBJECT

public:
    explicit KeyFrameLabelTreeWidget(QWidget *parent = 0);
    ~KeyFrameLabelTreeWidget();

    void setSceneNode(iris::SceneNodePtr node);

private:
    Ui::KeyFrameLabelTreeWidget *ui;
    iris::SceneNodePtr node;
    QHash<QString,KeyFrameGroup*> groups;

    void parseKeyFramesToGroups(iris::KeyFrameSetPtr frameSet);

    void buildTreeFromKeyFrameGroups(QHash<QString,KeyFrameGroup*> groups);
};

#endif // KEYFRAMELABELTREEWIDGET_H
