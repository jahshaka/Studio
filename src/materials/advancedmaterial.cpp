/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "../materials.h"
#include "advancematerial.h"
#include "../helpers/texturehelper.h"

AdvanceEffect::AdvanceEffect()
{
    tech = new Qt3DRender::QTechnique();
    tech->graphicsApiFilter()->setApi(Qt3DRender::QGraphicsApiFilter::OpenGL);
    tech->graphicsApiFilter()->setMajorVersion(3);
    tech->graphicsApiFilter()->setMinorVersion(2);
    tech->graphicsApiFilter()->setProfile(Qt3DRender::QGraphicsApiFilter::CoreProfile);

    //shader
    auto shader = new Qt3DRender::QShaderProgram();
    shader->setVertexShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/assets/shaders/advanced.vert"))));
    shader->setFragmentShaderCode(shader->loadSource(QUrl(QStringLiteral("qrc:/assets/shaders/advanced.frag"))));
    //shader->setVertexShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral("assets/shaders/advanced.vert"))));
    //shader->setFragmentShaderCode(shader->loadSource(QUrl::fromLocalFile(QStringLiteral("assets/shaders/advanced.frag"))));

    auto filterKey = new Qt3DRender::QFilterKey();
    filterKey->setName("renderingStyle");
    filterKey->setValue("forward");
    tech->addFilterKey(filterKey);

    auto pass = new Qt3DRender::QRenderPass();
    pass->setShaderProgram(shader);

    //required for transparent shading
    auto blendState = new Qt3DRender::QBlendEquationArguments();
    blendState->setSourceRgb(Qt3DRender::QBlendEquationArguments::SourceAlpha);
    blendState->setDestinationRgb(Qt3DRender::QBlendEquationArguments::OneMinusSourceAlpha);

    auto blendEquation = new Qt3DRender::QBlendEquation();
    blendEquation->setBlendFunction(Qt3DRender::QBlendEquation::Add);


    tech->addRenderPass(pass);
    this->addTechnique(tech);
}

AdvanceMaterial::AdvanceMaterial()
{
    diffuseTexture = new Qt3DRender::QTexture2D();
    diffuseTexture->setMagnificationFilter(Qt3DRender::QAbstractTexture::Linear);
    diffuseTexture->setMinificationFilter(Qt3DRender::QAbstractTexture::LinearMipMapLinear);
    diffuseTexture->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
    diffuseTexture->setGenerateMipMaps(true);
    //diffuseTexture->setMaximumAnisotropy(16.0f);

    normalTexture = new Qt3DRender::QTexture2D();
    normalTexture->setMagnificationFilter(Qt3DRender::QAbstractTexture::Linear);
    normalTexture->setMinificationFilter(Qt3DRender::QAbstractTexture::LinearMipMapLinear);
    normalTexture->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
    normalTexture->setGenerateMipMaps(true);
    //normalTexture->setMaximumAnisotropy(16.0f);

    specularTexture = new Qt3DRender::QTexture2D();
    specularTexture->setMagnificationFilter(Qt3DRender::QAbstractTexture::Linear);
    specularTexture->setMinificationFilter(Qt3DRender::QAbstractTexture::LinearMipMapLinear);
    specularTexture->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
    specularTexture->setGenerateMipMaps(true);
    //specularTexture->setMaximumAnisotropy(16.0f);

    reflectionTexture = new Qt3DRender::QTexture2D();
    reflectionTexture->setMagnificationFilter(Qt3DRender::QAbstractTexture::Linear);
    reflectionTexture->setMinificationFilter(Qt3DRender::QAbstractTexture::LinearMipMapLinear);
    reflectionTexture->setWrapMode(Qt3DRender::QTextureWrapMode(Qt3DRender::QTextureWrapMode::Repeat));
    reflectionTexture->setGenerateMipMaps(true);

    ambientColorParam = new Qt3DRender::QParameter("ambientCol",QVariant::fromValue(QColor(0,0,0,255)));

    useDiffuseTexParam = new Qt3DRender::QParameter("useDiffuseTex",QVariant::fromValue(false));
    diffuseTexParam = new Qt3DRender::QParameter("diffuseTexture",diffuseTexture);
    diffuseColorParam = new Qt3DRender::QParameter("diffuseCol",QVariant::fromValue(QColor(255,255,255,255)));

    useNormalTexParam = new Qt3DRender::QParameter("useNormalTex",QVariant::fromValue(false));
    normalTexParam = new Qt3DRender::QParameter("normalTexture",normalTexture);
    normalIntensityParam = new Qt3DRender::QParameter("normalIntensity",1.0f);

    useSpecularTexParam = new Qt3DRender::QParameter("useSpecularTex",QVariant::fromValue(false));
    specularTexParam = new Qt3DRender::QParameter("specularTexture",specularTexture);
    specularColorParam = new Qt3DRender::QParameter("specularCol",QVariant::fromValue(QColor(255,255,255,255)));
    shininessParam = new Qt3DRender::QParameter("shininess",0.0f);

    textureScaleParam = new Qt3DRender::QParameter("textureScale",QVariant::fromValue(1.0f));

    useReflectionTexParam = new Qt3DRender::QParameter("useReflectionTex",QVariant::fromValue(false));
    reflectionTexParam = new Qt3DRender::QParameter("reflectionTexture",reflectionTexture);
    reflectionInfluenceParam = new Qt3DRender::QParameter("reflectionFactor",0.0f);

    fogEnabledParam = new Qt3DRender::QParameter("fogEnabled",QVariant::fromValue(false));
    fogStartParam = new Qt3DRender::QParameter("fogStart",70.0f);
    fogEndParam = new Qt3DRender::QParameter("fogEnd",320.0f);
    fogColorParam = new Qt3DRender::QParameter("fogColor",QVariant::fromValue(QColor(200,200,200,255)));

    this->addParameter(ambientColorParam);

    this->addParameter(useDiffuseTexParam);
    this->addParameter(diffuseTexParam);
    this->addParameter(diffuseColorParam);

    this->addParameter(useNormalTexParam);
    this->addParameter(normalTexParam);
    this->addParameter(normalIntensityParam);

    this->addParameter(useSpecularTexParam);
    this->addParameter(specularTexParam);
    this->addParameter(specularColorParam);
    this->addParameter(shininessParam);

    this->addParameter(useReflectionTexParam);
    this->addParameter(reflectionTexParam);
    this->addParameter(reflectionInfluenceParam);

    this->addParameter(fogEnabledParam);
    this->addParameter(fogStartParam);
    this->addParameter(fogEndParam);
    this->addParameter(fogColorParam);

    this->addParameter(textureScaleParam);

    this->setEffect(new AdvanceEffect());
}

void AdvanceMaterial::setAmbientColor(QColor col)
{
    ambientColorParam->setValue(QVariant::fromValue(col));
}

QColor AdvanceMaterial::getAmbientColor()
{
    return ambientColorParam->value().value<QColor>();
}

void AdvanceMaterial::setDiffuseColor(QColor col)
{
    diffuseColorParam->setValue(QVariant::fromValue(col));
}

QColor AdvanceMaterial::getDiffuseColor()
{
    return diffuseColorParam->value().value<QColor>();
}

void AdvanceMaterial::setDiffuseTexture(Qt3DRender::QTextureImage* image)
{
    TextureHelper::clearImages(diffuseTexture);

    if(image==nullptr)
    {
        removeDiffuseTexture();
    }
    else
    {
        diffuseTexture->addTextureImage(image);
        useDiffuseTexParam->setValue(true);
    }

}

Qt3DRender::QTextureImage* AdvanceMaterial::getDiffuseTexture()
{
    auto img = TextureHelper::getImageFromTexture(diffuseTexture);
    return img;
    //img->source().toLocalFile();
}

QString AdvanceMaterial::getDiffuseTextureSource()
{
    auto img = TextureHelper::getImageFromTexture(diffuseTexture);
    if(img==nullptr)
        return QString();

    auto path = img->source().toLocalFile();
    return path;
}

void AdvanceMaterial::removeDiffuseTexture()
{
    TextureHelper::clearImages(diffuseTexture);
    useDiffuseTexParam->setValue(false);
}

void AdvanceMaterial::setNormalTexture(Qt3DRender::QTextureImage* image)
{
    TextureHelper::clearImages(normalTexture);

    if(image==nullptr)
    {
        removeNormalTexture();
    }
    else
    {
        normalTexture->addTextureImage(image);
        useNormalTexParam->setValue(true);
    }
}

Qt3DRender::QTextureImage* AdvanceMaterial::getNormalTexture()
{
    auto img = TextureHelper::getImageFromTexture(normalTexture);
    return img;
}

QString AdvanceMaterial::getNormalTextureSource()
{
    auto img = TextureHelper::getImageFromTexture(normalTexture);
    if(img==nullptr)
        return QString();

    auto path = img->source().toLocalFile();
    return path;
}

void AdvanceMaterial::removeNormalTexture()
{
    TextureHelper::clearImages(normalTexture);
    useNormalTexParam->setValue(false);
}

void AdvanceMaterial::setNormalIntensity(float intensity)
{
    normalIntensityParam->setValue(intensity);
}

float AdvanceMaterial::getNormalIntensity()
{
    return normalIntensityParam->value().value<float>();
}


void AdvanceMaterial::setSpecularTexture(Qt3DRender::QTextureImage* image)
{
    TextureHelper::clearImages(specularTexture);

    if(image==nullptr)
    {
        removeSpecularTexture();
    }
    else
    {
        specularTexture->addTextureImage(image);
        useSpecularTexParam->setValue(true);
    }
}

Qt3DRender::QTextureImage* AdvanceMaterial::getSpecularTexture()
{
    auto img = TextureHelper::getImageFromTexture(specularTexture);
    return img;
}

QString AdvanceMaterial::getSpecularTextureSource()
{
    auto img = TextureHelper::getImageFromTexture(specularTexture);
    if(img==nullptr)
        return QString();

    auto path = img->source().toLocalFile();
    return path;
}

void AdvanceMaterial::removeSpecularTexture()
{
    TextureHelper::clearImages(specularTexture);
    useSpecularTexParam->setValue(false);
}

void AdvanceMaterial::setSpecularColor(QColor col)
{
    specularColorParam->setValue(QVariant::fromValue(col));
}

QColor AdvanceMaterial::getSpecularColor()
{
    return specularColorParam->value().value<QColor>();
}

void AdvanceMaterial::setShininess(float shininess)
{
    shininessParam->setValue(shininess);
}

float AdvanceMaterial::getShininess()
{
    return shininessParam->value().value<float>();
}


void AdvanceMaterial::setReflectionTexture(Qt3DRender::QTextureImage* image)
{
    TextureHelper::clearImages(reflectionTexture);

    if(image==nullptr)
    {
        removeReflectionTexture();
    }
    else
    {
        reflectionTexture->addTextureImage(image);
        useReflectionTexParam->setValue(true);
    }
}

Qt3DRender::QTextureImage* AdvanceMaterial::getReflectionTexture()
{
    auto img = TextureHelper::getImageFromTexture(reflectionTexture);
    return img;
}

QString AdvanceMaterial::getReflectionTextureSource()
{
    auto img = TextureHelper::getImageFromTexture(specularTexture);
    if(img==nullptr)
        return QString();

    auto path = img->source().toLocalFile();
    return path;
}

void AdvanceMaterial::removeReflectionTexture()
{
    TextureHelper::clearImages(reflectionTexture);
    useReflectionTexParam->setValue(false);
}

void AdvanceMaterial::setReflectionInfluence(float intensity)
{
    reflectionInfluenceParam->setValue(intensity);
}

float AdvanceMaterial::getReflectionInfluence()
{
    return reflectionInfluenceParam->value().value<float>();
}

void AdvanceMaterial::setTextureScale(float scale)
{
    //qDebug()<<scale<<endl;
    textureScaleParam->setValue(scale);
}

float AdvanceMaterial::getTextureScale()
{
    return textureScaleParam->value().value<float>();
}

void AdvanceMaterial::setFogEnabled(bool enabled)
{
    fogEnabledParam->setValue(enabled);
}

void AdvanceMaterial::setFogColor(QColor color)
{
    fogColorParam->setValue(QVariant::fromValue(color));
}

void AdvanceMaterial::setFogStart(float fogStart)
{
    fogStartParam->setValue(fogStart);
}

void AdvanceMaterial::setFogEnd(float fogEnd)
{
    fogEndParam->setValue(fogEnd);
}

void AdvanceMaterial::updateFogParams(bool enabled,QColor color,float start,float end)
{
    fogEnabledParam->setValue(enabled);

    fogStartParam->setValue(start);
    fogEndParam->setValue(end);
    fogColorParam->setValue(QVariant::fromValue(color));
}

AdvanceMaterial* AdvanceMaterial::duplicate()
{
    auto mat = new AdvanceMaterial();

    mat->setAmbientColor(this->getAmbientColor());

    mat->setDiffuseColor(this->getDiffuseColor());
    auto tex = this->getDiffuseTextureSource();
    if(!tex.isEmpty())
        mat->setDiffuseTexture(TextureHelper::loadTexture(tex));

    mat->setNormalIntensity(this->getNormalIntensity());
    tex = this->getNormalTextureSource();
    if(!tex.isEmpty())
        mat->setNormalTexture(TextureHelper::loadTexture(tex));

    mat->setSpecularColor(this->getSpecularColor());
    mat->setShininess(this->getShininess());

    tex = this->getSpecularTextureSource();
    if(!tex.isEmpty())
        mat->setSpecularTexture(TextureHelper::loadTexture(tex));

    mat->setReflectionInfluence(this->getReflectionInfluence());

    tex = this->getReflectionTextureSource();
    if(!tex.isEmpty())
        mat->setReflectionTexture(TextureHelper::loadTexture(tex));

    return mat;
}

void AdvanceMaterial::applyPreset(MaterialPreset preset)
{
    this->setAmbientColor(preset.ambientColor);

    this->setDiffuseColor(preset.diffuseColor);
    auto tex = preset.diffuseTexture;
    if(tex.isEmpty())
        this->setDiffuseTexture(nullptr);
    else
        this->setDiffuseTexture(TextureHelper::loadTexture(tex));

    this->setNormalIntensity(preset.normalIntensity);
    tex = preset.normalTexture;
    if(tex.isEmpty())
        this->setNormalTexture(nullptr);
    else
        this->setNormalTexture(TextureHelper::loadTexture(tex));

    this->setSpecularColor(preset.specularColor);
    this->setShininess(preset.shininess);

    tex = preset.specularTexture;
    if(tex.isEmpty())
        this->setSpecularTexture(nullptr);
    else
        this->setSpecularTexture(TextureHelper::loadTexture(tex));

    this->setReflectionInfluence(preset.reflectionInfluence);

    tex = preset.reflectionTexture;
    if(tex.isEmpty())
        this->setReflectionTexture(nullptr);
    else
        this->setReflectionTexture(TextureHelper::loadTexture(tex));
}

/*
void AdvancedMaterial::setTexture(Qt3DRender::QAbstractTextureImage* tex)
{
    texture->addTextureImage(tex);
}

void AdvancedMaterial::setTexture(Qt3DRender::QAbstractTexture* tex)
{
    texParam->setValue(QVariant::fromValue(tex));
}

void AdvancedMaterial::setUseTexture(bool useTexture)
{
    useTexParam->setValue(useTexture);
}

void AdvancedMaterial::setColor(QColor color)
{
    colorParam->setValue(color);
}
*/
