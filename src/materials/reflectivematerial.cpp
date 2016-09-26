/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "../materials.h"
#include "reflectivematerial.h"
#include <Qt3DRender/qtexture.h>
#include "../helpers/texturehelper.h"

ReflectiveEffect::ReflectiveEffect()
{
    //is the profile that qt3d uses 3.2?
    tech = new Qt3DRender::QTechnique();
    tech->graphicsApiFilter()->setApi(Qt3DRender::QGraphicsApiFilter::OpenGL);
    tech->graphicsApiFilter()->setMajorVersion(3);
    tech->graphicsApiFilter()->setMinorVersion(2);
    tech->graphicsApiFilter()->setProfile(Qt3DRender::QGraphicsApiFilter::CoreProfile);

    auto filterKey = new Qt3DRender::QFilterKey();
    filterKey->setName("renderingStyle");
    filterKey->setValue("forward");
    tech->addFilterKey(filterKey);

    //shader
    auto shader = new Qt3DRender::QShaderProgram();
    //shader->setVertexShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/assets/shaders/reflective_material.vert"))));
    //shader->setFragmentShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/assets/shaders/reflective_material.frag"))));
    shader->setVertexShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral("assets/shaders/reflective_material.vert"))));
    shader->setFragmentShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral("assets/shaders/reflective_material.frag"))));
    auto pass = new Qt3DRender::QRenderPass();
    pass->setShaderProgram(shader);

    tech->addRenderPass(pass);
    this->addTechnique(tech);
}


ReflectiveMaterial::ReflectiveMaterial()
{
    texture = new Qt3DRender::QTexture2D();
    texture->setMagnificationFilter(Qt3DRender::QAbstractTexture::Linear);
    texture->setMinificationFilter(Qt3DRender::QAbstractTexture::LinearMipMapLinear);
    texture->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
    texture->setGenerateMipMaps(true);
    texture->setMaximumAnisotropy(16.0f);

    texParam = new Qt3DRender::QParameter("reflTexture",texture);

    normalTexture = new Qt3DRender::QTexture2D();
    normalTexture->setMagnificationFilter(Qt3DRender::QAbstractTexture::Linear);
    normalTexture->setMinificationFilter(Qt3DRender::QAbstractTexture::LinearMipMapLinear);
    normalTexture->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
    normalTexture->setGenerateMipMaps(true);
    normalTexture->setMaximumAnisotropy(16.0f);

    normalTexParam = new Qt3DRender::QParameter("normalTexture",normalTexture);

    this->addParameter(texParam);
    this->addParameter(normalTexParam);
    this->setEffect(new ReflectiveEffect());
}

void ReflectiveMaterial::setTexture(Qt3DRender::QAbstractTextureImage* tex)
{
    TextureHelper::clearImages(texture);
    texture->addTextureImage(tex);
}

void ReflectiveMaterial::setNormalTexture(Qt3DRender::QAbstractTextureImage* tex)
{
    TextureHelper::clearImages(normalTexture);
    normalTexture->addTextureImage(tex);
}

//todo: possible source of error, remove if possible
void ReflectiveMaterial::setTexture(Qt3DRender::QAbstractTexture* tex)
{
    texParam->setValue(QVariant::fromValue(tex));
}
