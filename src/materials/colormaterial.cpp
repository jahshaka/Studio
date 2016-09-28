/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materials.h"
#include "colormaterial.h"

ColorEffect::ColorEffect()
{
    tech = new Qt3DRender::QTechnique();
    tech->graphicsApiFilter()->setApi(Qt3DRender::QGraphicsApiFilter::OpenGL);
    tech->graphicsApiFilter()->setMajorVersion(3);
    tech->graphicsApiFilter()->setMinorVersion(2);
    tech->graphicsApiFilter()->setProfile(Qt3DRender::QGraphicsApiFilter::CoreProfile);

    //shader
    auto shader = new Qt3DRender::QShaderProgram();
    shader->setVertexShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral(":/app/shaders/color.vert"))));
    shader->setFragmentShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral(":/app/shaders/color.frag"))));

    auto filterKey = new Qt3DRender::QFilterKey();
    filterKey->setName("renderingStyle");
    filterKey->setValue("forward");
    tech->addFilterKey(filterKey);

    auto pass = new Qt3DRender::QRenderPass();
    pass->setShaderProgram(shader);

    tech->addRenderPass(pass);
    addTechnique(tech);
}


ColorMaterial::ColorMaterial()
{
    colorParam = new Qt3DRender::QParameter("color",QVariant::fromValue(QColor(255,255,255)));
    this->addParameter(colorParam);
    this->setEffect(new ColorEffect());
}

void ColorMaterial::setColor(QColor color)
{
    colorParam->setValue(QVariant::fromValue(color));
}
