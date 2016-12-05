#ifndef PROPERTYWIDGET_H
#define PROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

namespace jah3d
{
    class SceneNode;
}

class AccordianBladeWidget;
class TransformEditor;
class MaterialPropertyWidget;
class WorldPropertyWidget;
class LightPropertyWidget;
class FogPropertyWidget;

/**
 * This class shows the properties of selected nodes in the scene
 */
class SceneNodePropertiesWidget:public QWidget
{
    Q_OBJECT
public:
    SceneNodePropertiesWidget(QWidget* parent=nullptr);

    /**
     * sets active scene node to show properties for
     * @param sceneNode
     */
    void setSceneNode(QSharedPointer<jah3d::SceneNode> sceneNode);

private:
    void clearLayout(QLayout* layout);

private:
    QSharedPointer<jah3d::SceneNode> sceneNode;

    TransformEditor* transformWidget;
    AccordianBladeWidget* transformPropView;

    AccordianBladeWidget* meshPropView;

    MaterialPropertyWidget* materialPropView;
    //MeshPropertyWidget* meshPropView;
    LightPropertyWidget* lightPropView;
    WorldPropertyWidget* worldPropView;
    FogPropertyWidget* fogPropView;
};

#endif // PROPERTYWIDGET_H
