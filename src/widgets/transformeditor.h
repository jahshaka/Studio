#ifndef TRANSFORMEDITOR_H
#define TRANSFORMEDITOR_H

#include <QWidget>
#include <QSharedPointer>

namespace jah3d
{
    class SceneNode;
}

namespace Ui {
class TransformEditor;
}

class TransformEditor : public QWidget
{
    Q_OBJECT

public:
    explicit TransformEditor(QWidget *parent = 0);
    ~TransformEditor();

    /**
     *  sects active scene node
     * @param sceneNode
     */
    void setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode);

protected slots:
    /**
     * triggered when active scene node's properties gets updated externally (from gizmo, scripts, etc)
     */
    //void sceneNodeUpdated();

    /**
     * translation change callbacks
     */
    void xPosChanged(float value);
    void yPosChanged(float value);
    void zPosChanged(float value);

    /**
     * rotation change callbacks
     */
    void xRotChanged(float value);
    void yRotChanged(float value);
    void zRotChanged(float value);

    /**
     * scale change callbacks
     */
    void xScaleChanged(float value);
    void yScaleChanged(float value);
    void zScaleChanged(float value);

private:
    Ui::TransformEditor *ui;

    QSharedPointer<jah3d::SceneNode> sceneNode;
};

#endif // TRANSFORMEDITOR_H
