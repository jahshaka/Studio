/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materials.h"
#include "refractivematerial.h"
#include <Qt3DRender/qtexture.h>
#include "../helpers/texturehelper.h"

RefractiveEffect::RefractiveEffect()
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
    shader->setVertexShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral("assets/shaders/refractive_material.vert"))));
    shader->setFragmentShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral("assets/shaders/refractive_material.frag"))));

    auto pass = new Qt3DRender::QRenderPass();
    pass->setShaderProgram(shader);

    tech->addRenderPass(pass);
    this->addTechnique(tech);
}


RefractiveMaterial::RefractiveMaterial()
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

    iorParam = new Qt3DRender::QParameter("ior",1.3333f);

    this->addParameter(texParam);
    this->addParameter(normalTexParam);
    this->setEffect(new RefractiveEffect());
}

void RefractiveMaterial::setTexture(Qt3DRender::QAbstractTextureImage* tex)
{
    TextureHelper::clearImages(texture);

    texture->addTextureImage(tex);
}

void RefractiveMaterial::setNormalTexture(Qt3DRender::QAbstractTextureImage* tex)
{
    TextureHelper::clearImages(normalTexture);

    normalTexture->addTextureImage(tex);
}

//bad, very likely a source of future bug
void RefractiveMaterial::setTexture(Qt3DRender::QAbstractTexture* tex)
{
    texParam->setValue(QVariant::fromValue(tex));
}

void RefractiveMaterial::setIoR(float ior)
{
    iorParam->setValue(QVariant::fromValue(ior));
}

float RefractiveMaterial::getIoR()
{
    return iorParam->value().toFloat();
}
