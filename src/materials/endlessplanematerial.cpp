/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "../materials.h"
#include "endlessplanematerial.h"
#include "../helpers/texturehelper.h"

//ENDLESS PLANE
EndlessPlaneEffect::EndlessPlaneEffect()
{
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
    //shader->setVertexShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/assets/shaders/endless_plane.vert"))));
    //shader->setFragmentShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/assets/shaders/endless_plane.frag"))));
    shader->setVertexShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral("assets/shaders/endless_plane.vert"))));
    shader->setFragmentShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral("assets/shaders/endless_plane.frag"))));

    auto pass = new Qt3DRender::QRenderPass();
    //pass->addAnnotation(criterion);
    pass->setShaderProgram(shader);

    tech->addRenderPass(pass);
    addTechnique(tech);
}


EndlessPlaneMaterial::EndlessPlaneMaterial()
{
    texture = new Qt3DRender::QTexture2D();
    texture->setMagnificationFilter(Qt3DRender::QAbstractTexture::Linear);
    texture->setMinificationFilter(Qt3DRender::QAbstractTexture::LinearMipMapLinear);
    texture->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
    texture->setGenerateMipMaps(true);
    texture->setMaximumAnisotropy(16.0f);

    texParam = new Qt3DRender::QParameter("diffuseTexture",texture);
    textureScaleParam = new Qt3DRender::QParameter("texScale",0.05f);

    this->addParameter(texParam);
    this->addParameter(textureScaleParam);
    this->setEffect(new EndlessPlaneEffect());
}

void EndlessPlaneMaterial::setTexture(Qt3DRender::QAbstractTextureImage* tex)
{
    TextureHelper::clearImages(texture);
    texture->addTextureImage(tex);
}

void EndlessPlaneMaterial::setTexture(Qt3DRender::QAbstractTexture* tex)
{
    texture = tex;
    texParam->setValue(QVariant::fromValue(tex));
}

void EndlessPlaneMaterial::setTextureScale(float scale)
{
    textureScaleParam->setValue(scale);
}
