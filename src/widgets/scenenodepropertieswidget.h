/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef PROPERTYWIDGET_H
#define PROPERTYWIDGET_H

#include <QWidget>
#include <QSharedPointer>

namespace iris {
    class SceneNode;
}

class AccordianBladeWidget;
class TransformEditor;
class MaterialPropertyWidget;
class WorldPropertyWidget;
class LightPropertyWidget;
class FogPropertyWidget;
class EmitterPropertyWidget;
class NodePropertyWidget;
class MeshPropertyWidget;
class DemoPane;

/**
 * This class shows the properties of selected nodes in the scene
 */
class SceneNodePropertiesWidget : public QWidget
{
    Q_OBJECT
public:
    SceneNodePropertiesWidget(QWidget *parent = nullptr);

    /**
     * sets active scene node to show properties for
     * @param sceneNode
     */
    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);

    /**
     * Updates material properties if active scene node is a mesh
     */
    void refreshMaterial(const QString &matName);

private:
    void clearLayout(QLayout*);

private:
    QSharedPointer<iris::SceneNode> sceneNode;

    AccordianBladeWidget* transformPropView;
    TransformEditor* transformWidget;

    MaterialPropertyWidget* materialPropView;
    EmitterPropertyWidget* emitterPropView;
    // NodePropertyWidget* nodePropView;
    LightPropertyWidget* lightPropView;
    WorldPropertyWidget* worldPropView;
    FogPropertyWidget*  fogPropView;
    MeshPropertyWidget* meshPropView;
    // DemoPane* demoPane;
};

#endif // PROPERTYWIDGET_H
