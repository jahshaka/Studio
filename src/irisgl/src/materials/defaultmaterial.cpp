/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "../graphics/material.h"
#include "../graphics/texture.h"
#include "../graphics/texture2d.h"
#include "../materials/defaultmaterial.h"
#include "../core/scene.h"
#include "../core/irisutils.h"
#include <QFile>
#include <QTextStream>

#include <QOpenGLShader>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QColor>

#include <QOpenGLFunctions_3_2_Core>

namespace iris
{

DefaultMaterial::DefaultMaterial()
{
    setTextureCount(4);

    this->createProgramFromShaderSource(":assets/shaders/default_material.vert",
                                        ":assets/shaders/default_material.frag");

    program->bind();
    program->setUniformValue("u_useDiffuseTex",false);
    program->setUniformValue("u_useNormalTex",false);
    program->setUniformValue("u_useReflectionTex",false);
    program->setUniformValue("u_useSpecularTex",false);
    program->setUniformValue("u_material.diffuse",QVector3D(1,0,0));

    textureScale = 1.0f;
    ambientColor = QColor(0,0,0);

    useNormalTex = false;
    normalIntensity = 1.0f;

    diffuseColor = QColor(255,255,255);
    useDiffuseTex = false;

    shininess = 20.0f;
    useSpecularTex = false;
    specularColor = QColor(20,20,20);

    reflectionInfluence = 0.0f;
    useReflectionTex = false;

    this->setRenderLayer((int)RenderLayer::Opaque);
}

void DefaultMaterial::begin(QOpenGLFunctions_3_2_Core* gl,ScenePtr scene)
{
    program->bind();

    bindTextures(gl);

    //set params
    program->setUniformValue("u_material.diffuse",QVector3D(diffuseColor.redF(),diffuseColor.greenF(),diffuseColor.blueF()));

    const QColor& sceneAmbient = scene->ambientColor;
    auto finalAmbient = QVector3D(ambientColor.redF() + sceneAmbient.redF(),
                                  ambientColor.greenF() + sceneAmbient.greenF(),
                                  ambientColor.blueF() + sceneAmbient.blueF());
    program->setUniformValue("u_material.ambient",finalAmbient);
    program->setUniformValue("u_material.specular",QVector3D(specularColor.redF(),specularColor.greenF(),specularColor.blueF()));
    program->setUniformValue("u_material.shininess",shininess);

    program->setUniformValue("u_textureScale", this->textureScale);

    program->setUniformValue("u_normalIntensity",normalIntensity);
    program->setUniformValue("u_reflectionInfluence",reflectionInfluence);

    program->setUniformValue("u_useDiffuseTex",useDiffuseTex);
    program->setUniformValue("u_useNormalTex",useNormalTex);
    program->setUniformValue("u_useSpecularTex",useSpecularTex);
    program->setUniformValue("u_useReflectionTex",useReflectionTex);

}

void DefaultMaterial::end(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene)
{
    //unset textures
    Material::end(gl, scene);
}

void DefaultMaterial::setDiffuseTexture(QSharedPointer<Texture2D> tex)
{
    if(!!tex)
    {
        useDiffuseTex = true;
        addTexture("u_diffuseTexture",tex);
    }
    else
    {
        useDiffuseTex = false;
        removeTexture("u_diffuseTexture");
    }

    diffuseTexture=tex;
}

QString DefaultMaterial::getDiffuseTextureSource()
{
    if(!!diffuseTexture)
    {
        return diffuseTexture->source;
    }

    return QString::null;
}

void DefaultMaterial::setAmbientColor(QColor col)
{
    ambientColor = col;
}

void DefaultMaterial::setDiffuseColor(QColor col)
{
    diffuseColor = col;
}

QColor DefaultMaterial::getDiffuseColor()
{
    return diffuseColor;
}

void DefaultMaterial::setNormalTexture(QSharedPointer<Texture2D> tex)
{
    if(!!tex)
    {
        useNormalTex = true;
        addTexture("u_normalTexture",tex);
    }
    else
    {
        useNormalTex = false;
        removeTexture("u_normalTexture");
    }

    normalTexture=tex;
}

QString DefaultMaterial::getNormalTextureSource()
{
    if(!!normalTexture)
    {
        return normalTexture->source;
    }

    return QString::null;
}

void DefaultMaterial::setNormalIntensity(float intensity)
{
    normalIntensity = intensity;
}

float DefaultMaterial::getNormalIntensity()
{
    return normalIntensity;
}


void DefaultMaterial::setSpecularTexture(QSharedPointer<Texture2D> tex)
{
    if(!!tex)
    {
        useSpecularTex = true;
        addTexture("u_specularTexture",tex);
    }
    else
    {
        useSpecularTex = false;
        removeTexture("u_specularTexture");
    }

    specularTexture=tex;
}

QString DefaultMaterial::getSpecularTextureSource()
{
    if(!!specularTexture)
    {
        return specularTexture->source;
    }

    return QString::null;
}

void DefaultMaterial::setSpecularColor(QColor col)
{
    specularColor = col;
}

QColor DefaultMaterial::getSpecularColor()
{
    return specularColor;
}

void DefaultMaterial::setShininess(float shininess)
{
    this->shininess = shininess;
}

float DefaultMaterial::getShininess()
{
    return shininess;
}


void DefaultMaterial::setReflectionTexture(QSharedPointer<Texture2D> tex)
{
    if(!!tex)
    {

        useReflectionTex = true;
        addTexture("u_reflectionTexture",tex);
    }
    else
    {
        useReflectionTex = false;
        removeTexture("u_reflectionTexture");
    }

    reflectionTexture=tex;
}

QString DefaultMaterial::getReflectionTextureSource()
{
    if(!!reflectionTexture)
    {
        return reflectionTexture->source;
    }

    return QString::null;
}

void DefaultMaterial::setReflectionInfluence(float intensity)
{
    reflectionInfluence = intensity;
}

float DefaultMaterial::getReflectionInfluence()
{
    return reflectionInfluence;
}

void DefaultMaterial::setTextureScale(float scale)
{
    this->textureScale = scale;
}

float DefaultMaterial::getTextureScale()
{
    return textureScale;
}

}
