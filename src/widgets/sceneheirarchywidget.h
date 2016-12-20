#ifndef SCENEHEIRARCHYWIDGET_H
#define SCENEHEIRARCHYWIDGET_H

#include <QWidget>
#include <QMap>

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
    void populateTree(QTreeWidgetItem* parentNode,QSharedPointer<iris::SceneNode> sceneNode);

    //maps scene nodes to their widgetitems
    //QMap<long,QTreeWidgetItem*> nodeList;
    QMap<long,QSharedPointer<iris::SceneNode>> nodeList;
    QMap<long,QTreeWidgetItem*> treeItemList;

private:
    Ui::SceneHeirarchyWidget *ui;
    QSharedPointer<iris::Scene> scene;
    QSharedPointer<iris::SceneNode> selectedNode;
    MainWindow* mainWindow;

signals:
    void sceneNodeSelected(QSharedPointer<iris::SceneNode> sceneNode);
};

#endif // SCENEHEIRARCHYWIDGET_H
