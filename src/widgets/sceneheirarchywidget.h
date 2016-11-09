#ifndef SCENEHEIRARCHYWIDGET_H
#define SCENEHEIRARCHYWIDGET_H

#include <QWidget>
#include <QMap>

namespace Ui {
class SceneHeirarchyWidget;
}

namespace jah3d
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

    void setScene(QSharedPointer<jah3d::Scene> scene);
    void setMainWindow(MainWindow* mainWin);

protected slots:
    void treeItemSelected(QTreeWidgetItem* item);
    void treeItemChanged(QTreeWidgetItem* item,int index);
    void sceneTreeCustomContextMenu(const QPoint &);

    void renameNode();
    void deleteNode();
    void duplicateNode();

    /*
    void addTorus();
    void addCube();
    void addSphere();
    void addCylinder();
    void addTexturedPlane();
    void addPointLight();
    void addSpotLight();
    void addDirectionalLight();
    void addMesh();
    void addViewPoint();
    */

private:
    void repopulateTree();
    void populateTree(QTreeWidgetItem* parentNode,QSharedPointer<jah3d::SceneNode> sceneNode);

    QMap<long,QSharedPointer<jah3d::SceneNode>> nodeList;

private:
    Ui::SceneHeirarchyWidget *ui;
    QSharedPointer<jah3d::Scene> scene;
    QSharedPointer<jah3d::SceneNode> selectedNode;
    MainWindow* mainWindow;

signals:
    void sceneNodeSelected(QSharedPointer<jah3d::SceneNode> sceneNode);
};

#endif // SCENEHEIRARCHYWIDGET_H
