 /**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materials.h"
#include "gizmomaterial.h"

//GIZMO MATERIAL
GizmoEffect::GizmoEffect()
{
    tech = new Qt3DRender::QTechnique();
    tech->graphicsApiFilter()->setApi(Qt3DRender::QGraphicsApiFilter::OpenGL);
    tech->graphicsApiFilter()->setMajorVersion(3);
    tech->graphicsApiFilter()->setMinorVersion(2);
    tech->graphicsApiFilter()->setProfile(Qt3DRender::QGraphicsApiFilter::CoreProfile);

    //shader
    auto shader = new Qt3DRender::QShaderProgram();
    shader->setVertexShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/app/shaders/gizmo.vert"))));
    shader->setFragmentShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/app/shaders/gizmo.frag"))));

    auto filterKey = new Qt3DRender::QFilterKey();
    filterKey->setName("renderingStyle");
    filterKey->setValue("forward");
    tech->addFilterKey(filterKey);

    auto pass = new Qt3DRender::QRenderPass();

    auto blendState = new Qt3DRender::QBlendEquationArguments();
    blendState->setSourceRgb(Qt3DRender::QBlendEquationArguments::SourceAlpha);
    blendState->setDestinationRgb(Qt3DRender::QBlendEquationArguments::OneMinusSourceAlpha);

    auto blendEquation = new Qt3DRender::QBlendEquation();
    blendEquation->setBlendFunction(Qt3DRender::QBlendEquation::Add);
    pass->addRenderState(blendState);
    pass->addRenderState(blendEquation);

   // pass->addAnnotation(criterion);
    pass->setShaderProgram(shader);

    tech->addRenderPass(pass);
    addTechnique(tech);
}


GizmoMaterial::GizmoMaterial()
{
    colorParam = new Qt3DRender::QParameter("color",QVariant::fromValue(QColor(255,255,255)));
    showParam = new Qt3DRender::QParameter("showHalf",QVariant::fromValue(false));

    this->addParameter(colorParam);
    this->addParameter(showParam);
    this->setEffect(new GizmoEffect());
}

void GizmoMaterial::setColor(QColor color)
{
    colorParam->setValue(QVariant::fromValue(color));
}

void GizmoMaterial::showHalf(bool shouldShowHalf)
{
    showParam->setValue(shouldShowHalf);
}
