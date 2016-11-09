#ifndef SCENEHEIRARCHYWIDGET_H
#define SCENEHEIRARCHYWIDGET_H

#include <QWidget>

namespace Ui {
class SceneHeirarchyWidget;
}

namespace jah3d
{
    class Scene;
    class SceneNode;
}

class QTreeWidgetItem;

class SceneHeirarchyWidget : public QWidget
{
    Q_OBJECT

public:
    explicit SceneHeirarchyWidget(QWidget *parent = 0);
    ~SceneHeirarchyWidget();

    void setScene(QSharedPointer<jah3d::Scene> scene);

protected slots:
    void sceneNodeSelected(QTreeWidgetItem* item);
    void sceneTreeItemChanged(QTreeWidgetItem* item,int index);

private:
    void repopulateTree();
    void populateTree(QTreeWidgetItem* parentNode,QSharedPointer<jah3d::SceneNode> sceneNode);

private:
    Ui::SceneHeirarchyWidget *ui;
    QSharedPointer<jah3d::Scene> scene;
};

#endif // SCENEHEIRARCHYWIDGET_H
