/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#include "../materials/materials.h"
#include "skyboxnode.h"
#include <Qt3DRender/qtexture.h>
#include <Qt3DExtras/QCuboidMesh>
#include <Qt3DCore/QEntity>
#include <Qt3DCore/QTransform>
#include "scenenodes.h"


EquiRectSkyMaterial::EquiRectSkyMaterial()
{
    effect = new Qt3DRender::QEffect();

    tech = new Qt3DRender::QTechnique();
    tech->graphicsApiFilter()->setApi(Qt3DRender::QGraphicsApiFilter::OpenGL);
    tech->graphicsApiFilter()->setMajorVersion(3);
    tech->graphicsApiFilter()->setMinorVersion(2);
    tech->graphicsApiFilter()->setProfile(Qt3DRender::QGraphicsApiFilter::CoreProfile);

    auto shader = new Qt3DRender::QShaderProgram();
    shader->setVertexShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/app/shaders/equisky.vert"))));
    shader->setFragmentShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/app/shaders/equisky.frag"))));

    auto filterKey = new Qt3DRender::QFilterKey();
    filterKey->setName("renderingStyle");
    filterKey->setValue("forward");
    tech->addFilterKey(filterKey);

    auto pass = new Qt3DRender::QRenderPass();
    auto cullFront = new Qt3DRender::QCullFace();
    cullFront->setMode(Qt3DRender::QCullFace::Front);
    auto depthTest = new Qt3DRender::QDepthTest();

    depthTest->setDepthFunction(Qt3DRender::QDepthTest::LessOrEqual);
    pass->addRenderState(cullFront);
    pass->addRenderState(depthTest);

    pass->setShaderProgram(shader);
    tech->addRenderPass(pass);
    effect->addTechnique(tech);
    this->setEffect(effect);

    texture = new Qt3DRender::QTexture2D();
    texture->setMagnificationFilter(Qt3DRender::QAbstractTexture::Linear);
    texture->setMinificationFilter(Qt3DRender::QAbstractTexture::Linear);
    //texture->setMinificationFilter(Qt3DRender::QAbstractTexture::LinearMipMapLinear);
    texture->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
    //texture->setGenerateMipMaps(true);

    texParam = new Qt3DRender::QParameter("tex",texture);
    this->addParameter(texParam);
    //texture->setMaximumAnisotropy(16.0f);
}

void EquiRectSkyMaterial::setTexture(QString skyTexture)
{
    this->clearTexture();
    auto tex = new Qt3DRender::QTextureImage();
    tex->setSource(QUrl::fromLocalFile(skyTexture));
\
    texture->addTextureImage(tex);
    //texParam->setValue(QVariant::fromValue(static_cast<Qt3DRender::QAbstractTexture*>(tex)));
}

void EquiRectSkyMaterial::clearTexture()
{
    auto images = texture->textureImages();
    for(auto img:images)
        texture->removeTextureImage(img);
}

SkyBoxNode::SkyBoxNode(Qt3DCore::QEntity* parent):
    SceneNode(parent)
{
    cube = new Qt3DExtras::QCuboidMesh();
    material = new EquiRectSkyMaterial();
    transform = new Qt3DCore::QTransform();
    transform->setScale(1000);

    entity->addComponent(cube);
    entity->addComponent(material);
    entity->addComponent(transform);
}

void SkyBoxNode::setTexture(QString skyTexture)
{
    material->setTexture(skyTexture);
    this->skyTexture = skyTexture;
}

QString SkyBoxNode::getTexture()
{
    return skyTexture;
}

void SkyBoxNode::clearTexture()
{
    material->clearTexture();
}

SkyBoxNode* SkyBoxNode::createSky()
{
    auto sky = new SkyBoxNode(new Qt3DCore::QEntity());
    return sky;
}


