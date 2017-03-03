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

namespace iris
{
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
class DemoPane;

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
    void setSceneNode(QSharedPointer<iris::SceneNode> sceneNode);

    /**
     * Updates material properties if active scene node is a mesh
     */
    void refreshMaterial();

private:
    void clearLayout(QLayout* layout);

private:
    QSharedPointer<iris::SceneNode> sceneNode;

    TransformEditor* transformWidget;
    AccordianBladeWidget* transformPropView;

    AccordianBladeWidget* meshPropView;

    MaterialPropertyWidget* materialPropView;
    EmitterPropertyWidget* emitterPropView;
    //MeshPropertyWidget* meshPropView;
    NodePropertyWidget* nodePropView;
    LightPropertyWidget* lightPropView;
    WorldPropertyWidget* worldPropView;

    // this should be under world.. why does it have its own widget?
    // @TODO: also add an option to customize shadows in the world settings..
    FogPropertyWidget* fogPropView;

//    DemoPane* demoPane;
};

#endif // PROPERTYWIDGET_H
