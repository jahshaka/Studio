/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materials.h"
#include "billboardmaterial.h"

//BILLBOARD MATERIAL
BillboardEffect::BillboardEffect()
{
    tech = new Qt3DRender::QTechnique();
    tech->graphicsApiFilter()->setApi(Qt3DRender::QGraphicsApiFilter::OpenGL);
    tech->graphicsApiFilter()->setMajorVersion(3);
    tech->graphicsApiFilter()->setMinorVersion(2);
    tech->graphicsApiFilter()->setProfile(Qt3DRender::QGraphicsApiFilter::CoreProfile);

    //shader
    auto shader = new Qt3DRender::QShaderProgram();
    shader->setVertexShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/app/shaders/billboard.vert"))));
    shader->setFragmentShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/app/shaders/billboard.frag"))));

    auto filterKey = new Qt3DRender::QFilterKey();
    filterKey->setName("renderingStyle");
    filterKey->setValue("forward");
    tech->addFilterKey(filterKey);

    auto pass = new Qt3DRender::QRenderPass();
    //pass->addAnnotation(criterion);
    pass->setShaderProgram(shader);

    //required for transparent shading
    auto blendState = new Qt3DRender::QBlendEquationArguments();
    blendState->setSourceRgb(Qt3DRender::QBlendEquationArguments::SourceAlpha);
    blendState->setDestinationRgb(Qt3DRender::QBlendEquationArguments::OneMinusSourceAlpha);

    auto blendEquation = new Qt3DRender::QBlendEquation();
    blendEquation->setBlendFunction(Qt3DRender::QBlendEquation::Add);

    tech->addRenderPass(pass);
    addTechnique(tech);
}


BillboardMaterial::BillboardMaterial()
{
    texture = new Qt3DRender::QTexture2D();
    texture->setMagnificationFilter(Qt3DRender::QAbstractTexture::Linear);
    texture->setMinificationFilter(Qt3DRender::QAbstractTexture::LinearMipMapLinear);
    texture->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
    texture->setGenerateMipMaps(true);
    texture->setMaximumAnisotropy(16.0f);

    texParam = new Qt3DRender::QParameter("tex",texture);
    useTexParam = new Qt3DRender::QParameter("useTexture",true);
    colorParam = new Qt3DRender::QParameter("color",QColor(255,255,255,255));

    this->addParameter(texParam);
    this->addParameter(useTexParam);
    this->addParameter(colorParam);
    this->setEffect(new BillboardEffect());
}

void BillboardMaterial::setTexture(Qt3DRender::QAbstractTextureImage* tex)
{
    texture->addTextureImage(tex);
}

void BillboardMaterial::setTexture(Qt3DRender::QAbstractTexture* tex)
{
    texParam->setValue(QVariant::fromValue(tex));
}

void BillboardMaterial::setUseTexture(bool useTexture)
{
    useTexParam->setValue(useTexture);
}

void BillboardMaterial::setColor(QColor color)
{
    colorParam->setValue(color);
}
