/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materials.h"
#include "flatshadedmaterial.h"
#include <Qt3DRender/qtexture.h>

FlatShadedEffect::FlatShadedEffect()
{
    //is the profile that qt3d uses 3.2?
    tech = new Qt3DRender::QTechnique();
    tech->graphicsApiFilter()->setApi(Qt3DRender::QGraphicsApiFilter::OpenGL);
    tech->graphicsApiFilter()->setMajorVersion(3);
    tech->graphicsApiFilter()->setMinorVersion(2);
    tech->graphicsApiFilter()->setProfile(Qt3DRender::QGraphicsApiFilter::CoreProfile);

    //shader
    auto shader = new Qt3DRender::QShaderProgram();
    shader->setVertexShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral("qrc:/app/shaders/flat.vert"))));
    shader->setFragmentShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral("qrc:/app/shaders/flat.frag"))));

    //criteria for using this shader
    auto filterKey = new Qt3DRender::QFilterKey();
    filterKey->setName("renderingStyle");
    filterKey->setValue("forward");
    tech->addFilterKey(filterKey);

    //full definition of pass consists of criterion and shader
    auto pass = new Qt3DRender::QRenderPass();
    pass->setShaderProgram(shader);

    //required for transparent shading
    auto blendState = new Qt3DRender::QBlendEquationArguments();
    blendState->setSourceRgb(Qt3DRender::QBlendEquationArguments::SourceAlpha);
    blendState->setDestinationRgb(Qt3DRender::QBlendEquationArguments::OneMinusSourceAlpha);

    auto blendEquation = new Qt3DRender::QBlendEquation();
    blendEquation->setBlendFunction(Qt3DRender::QBlendEquation::Add);
    pass->addRenderState(blendState);
    pass->addRenderState(blendEquation);

    auto cull = new Qt3DRender::QCullFace();
    cull->setMode(Qt3DRender::QCullFace::CullingMode::NoCulling);
    pass->addRenderState(cull);

    tech->addRenderPass(pass);
    addTechnique(tech);
}


FlatShadedMaterial::FlatShadedMaterial()
{
    texture = new Qt3DRender::QTexture2D();
    texture->setMagnificationFilter(Qt3DRender::QAbstractTexture::Linear);
    texture->setMinificationFilter(Qt3DRender::QAbstractTexture::LinearMipMapLinear);
    texture->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
    texture->setGenerateMipMaps(true);
    texture->setMaximumAnisotropy(16.0f);

    texParam = new Qt3DRender::QParameter("tex",texture);

    this->addParameter(texParam);
    this->setEffect(new FlatShadedEffect());
}

void FlatShadedMaterial::setTexture(Qt3DRender::QAbstractTexture* tex)
{
    texParam->setValue(QVariant::fromValue(tex));
}
