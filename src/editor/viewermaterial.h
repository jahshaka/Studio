/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef VIEWERMATERIAL_H
#define VIEWERMATERIAL_H

#include "../../irisgl/src/graphics/material.h"
#include "../../irisgl/src/irisglfwd.h"
#include <QOpenGLShaderProgram>
#include <QColor>
#include <QSharedPointer>

class QOpenGLFunctions_3_2_Core;
class ViewerMaterial;
typedef QSharedPointer<ViewerMaterial> ViewerMaterialPtr;

class ViewerMaterial : public iris::Material
{
public:

    void setTexture(iris::Texture2DPtr tex);
	iris::Texture2DPtr getTexture();

    void begin(iris::GraphicsDevicePtr device, iris::ScenePtr scene) override;
    void end(iris::GraphicsDevicePtr device, iris::ScenePtr scene) override;

    static ViewerMaterialPtr create();
private:
    ViewerMaterial();

	iris::Texture2DPtr texture;
};

#endif // VIEWERMATERIAL_H
