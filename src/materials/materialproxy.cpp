/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#include "materialproxy.h"

#include <Qt3DRender/QMaterial>
#include <Qt3DExtras/QPhongMaterial>
#include <Qt3DExtras/QDiffuseMapMaterial>
#include <Qt3DExtras/QDiffuseSpecularMapMaterial>
#include <Qt3DExtras/QNormalDiffuseMapMaterial>
#include <Qt3DRender/QTextureImage>
#include "materials.h"

#include "../scenegraph/scenenodes.h"

using namespace Qt3DRender;

void MaterialProxy::setData(MaterialData* data)
{
    setDiffuse(data->diffuse);
    setSpecular(data->specular);
    setAmbient(data->ambient);
    setShininess(data->shininess);
    //setAlpha(data->alpha);
    setDiffuseMap(data->diffuseTex);
    setNormalMap(data->normalTex);
    setSpecularMap(data->specularTex);
}

MaterialData* MaterialProxy::getData()
{
    auto data = new MaterialData();
    data->diffuse = this->getDiffuse();
    data->specular = this->getSpecular();
    data->ambient = this->getAmbient();
    data->shininess = this->getShininess();
    //data->alpha = this->getAlpha();
    data->diffuseTex = this->getDiffuseMap();
    data->normalTex = this->getNormalMap();
    data->specularTex = this->getSpecularMap();

    return data;
}

void MaterialProxy::setDiffuse(QColor col)
{
    switch(type)
    {
    case MaterialType::Default:
        {
            auto phong = static_cast<Qt3DExtras::QPhongMaterial*>(material);
            phong->setDiffuse(col);
        }
        break;
    default:
        break;
    }
}

void MaterialProxy::setSpecular(QColor col)
{
    switch(type)
    {
    case MaterialType::Default:
        {
            auto phong = static_cast<Qt3DExtras::QPhongMaterial*>(material);
            phong->setSpecular(col);
        }
        break;
    case MaterialType::DiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseMapMaterial*>(material);
            mat->setSpecular(col);
        }
        break;
    case MaterialType::NormalDiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QNormalDiffuseMapMaterial*>(material);
            mat->setSpecular(col);
        }
        break;
    default:
        break;
    }
}

void MaterialProxy::setAmbient(QColor col)
{
    switch(type)
    {
    case MaterialType::Default:
        {
            auto phong = static_cast<Qt3DExtras::QPhongMaterial*>(material);
            phong->setAmbient(col);
        }
        break;
    case MaterialType::DiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseMapMaterial*>(material);
            mat->setAmbient(col);
        }
        break;
    case MaterialType::DiffuseSpecularMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseSpecularMapMaterial*>(material);
            mat->setAmbient(col);
        }
        break;
    case MaterialType::NormalDiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QNormalDiffuseMapMaterial*>(material);
            mat->setAmbient(col);
        }
        break;
    default:
        break;
    }
}

void MaterialProxy::setShininess(float shininess)
{
    switch(type)
    {
    case MaterialType::Default:
        {
            auto mat = static_cast<Qt3DExtras::QPhongMaterial*>(material);
            mat->setShininess(shininess);
        }
        break;
    case MaterialType::DiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseMapMaterial*>(material);
            mat->setShininess(shininess);
        }
        break;
    case MaterialType::DiffuseSpecularMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseSpecularMapMaterial*>(material);
            mat->setShininess(shininess);
        }
        break;
    case MaterialType::NormalDiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QNormalDiffuseMapMaterial*>(material);
            mat->setShininess(shininess);
        }
        break;
    default:
        break;
    }
}

void MaterialProxy::setAlpha(float alpha)
{
    Q_UNUSED(alpha);
}

float MaterialProxy::getShininess()
{
    switch(type)
    {
    case MaterialType::Default:
        {
            auto phong = static_cast<Qt3DExtras::QPhongMaterial*>(material);
            return phong->shininess();
        }
    case MaterialType::DiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseMapMaterial*>(material);
            return mat->shininess();
        }
    case MaterialType::DiffuseSpecularMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseSpecularMapMaterial*>(material);
            return mat->shininess();
        }
    case MaterialType::NormalDiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QNormalDiffuseMapMaterial*>(material);
            return mat->shininess();
        }
    default:
        break;
    }

    return 0;
}

float MaterialProxy::getAlpha()
{
    return 0;
}

QColor MaterialProxy::getDiffuse()
{
    switch(type)
    {
    case MaterialType::Default:
        {
            auto phong = static_cast<Qt3DExtras::QPhongMaterial*>(material);
            return phong->diffuse();
        }
    default:
        break;
    }

    return QColor::fromRgb(0,0,0);
}

QColor MaterialProxy::getSpecular()
{
    switch(type)
    {
    case MaterialType::Default:
        {
            auto phong = static_cast<Qt3DExtras::QPhongMaterial*>(material);
            return phong->specular();
        }
    case MaterialType::DiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseMapMaterial*>(material);
            return mat->specular();
        }
    case MaterialType::NormalDiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QNormalDiffuseMapMaterial*>(material);
            return mat->specular();
        }
    default:
        break;
    }

    return QColor::fromRgb(0,0,0);
}

QColor MaterialProxy::getAmbient()
{
    switch(type)
    {
    case MaterialType::Default:
        {
            auto phong = static_cast<Qt3DExtras::QPhongMaterial*>(material);
            return phong->ambient();
        }
    case MaterialType::DiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseMapMaterial*>(material);
            return mat->ambient();
        }
    case MaterialType::DiffuseSpecularMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseSpecularMapMaterial*>(material);
            return mat->ambient();
        }
    case MaterialType::NormalDiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QNormalDiffuseMapMaterial*>(material);
            return mat->ambient();
        }
    default:
        break;
    }

    return QColor::fromRgb(0,0,0);
}

void MaterialProxy::setDiffuseMap(QString path)
{
    if(path.isEmpty())
        return;

    //putting it here is bad, but it'll do for now
    Qt3DRender::QTextureImage *tex = new Qt3DRender::QTextureImage();
    tex->setSource(QUrl::fromLocalFile(path));
    //tex->setSource(QUrl(path));


    switch(type)
    {
    case MaterialType::DiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseMapMaterial*>(material);
            return mat->diffuse()->addTextureImage(tex);
        }
        break;
    case MaterialType::DiffuseSpecularMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseSpecularMapMaterial*>(material);
            return mat->diffuse()->addTextureImage(tex);
        }
        break;
    case MaterialType::NormalDiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QNormalDiffuseMapMaterial*>(material);
            return mat->diffuse()->addTextureImage(tex);
        }
        break;
    case MaterialType::Reflection:
        {
            auto mat = static_cast<ReflectiveMaterial*>(material);
            return mat->setTexture(tex);
        }
        break;
    case MaterialType::Refraction:
        {
            auto mat = static_cast<RefractiveMaterial*>(material);
            return mat->setTexture(tex);
        }
        break;
    default:
        break;
    }
}

void MaterialProxy::setSpecularMap(QString path)
{
    if(path.isEmpty())
        return;
    //putting it here is bad, but it'll do for now
    Qt3DRender::QTextureImage *tex = new Qt3DRender::QTextureImage();
    tex->setSource(QUrl::fromLocalFile(path));

    switch(type)
    {
    case MaterialType::DiffuseSpecularMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseSpecularMapMaterial*>(material);
            return mat->diffuse()->addTextureImage(tex);
        }
        break;
    default:
        break;
    }
}

void MaterialProxy::setNormalMap(QString path)
{
    if(path.isEmpty())
        return;
    //putting it here is bad, but it'll do for now
    Qt3DRender::QTextureImage *tex = new Qt3DRender::QTextureImage();
    tex->setSource(QUrl::fromLocalFile(path));

    switch(type)
    {
    case MaterialType::NormalDiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QNormalDiffuseMapMaterial*>(material);
            return mat->normal()->addTextureImage(tex);
        }
        break;
    case MaterialType::Reflection:
        {
            auto mat = static_cast<ReflectiveMaterial*>(material);
            return mat->setNormalTexture(tex);
        }
        break;
    case MaterialType::Refraction:
        {
            auto mat = static_cast<RefractiveMaterial*>(material);
            return mat->setNormalTexture(tex);
        }
        break;
    default:
        break;
    }
}

QString MaterialProxy::getDiffuseMap()
{
    QString value="";
    Qt3DRender::QAbstractTextureImage* img = nullptr;

    switch(type)
    {
    case MaterialType::DiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseMapMaterial*>(material);
            auto list = mat->diffuse()->textureImages();
            if(list.size()>0)
                img = list[0];
        }
        break;
    case MaterialType::DiffuseSpecularMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseSpecularMapMaterial*>(material);
            auto list = mat->diffuse()->textureImages();
            if(list.size()>0)
                img = list[0];
        }
        break;
    case MaterialType::NormalDiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QNormalDiffuseMapMaterial*>(material);
            auto list = mat->diffuse()->textureImages();
            if(list.size()>0)
                img = list[0];
        }
        break;
    case MaterialType::Reflection:
        {
            auto mat = static_cast<ReflectiveMaterial*>(material);
            auto list = mat->getTexture()->textureImages();
            if(list.size()>0)
                img = list[0];
        }
        break;
    case MaterialType::Refraction:
        {
            auto mat = static_cast<RefractiveMaterial*>(material);
            auto list = mat->getTexture()->textureImages();
            if(list.size()>0)
                img = list[0];
        }
        break;
    default:
        break;
    }

    if(img==nullptr)
        return "";

    //all images can be safely casted to Qt3DRender::QTextureImage*
    auto url = static_cast<Qt3DRender::QTextureImage*>(img)->source();
    return url.toLocalFile();
}

QString MaterialProxy::getSpecularMap()
{
    Qt3DRender::QAbstractTextureImage* img = nullptr;

    switch(type)
    {
    case MaterialType::DiffuseSpecularMap:
        {
            auto mat = static_cast<Qt3DExtras::QDiffuseSpecularMapMaterial*>(material);
            auto list = mat->diffuse()->textureImages();
            if(list.size()>0)
                img = list[0];
        }
        break;
    default:
        break;
    }

    if(img==nullptr)
        return "";

    //all images can be safely casted to Qt3DRender::QTextureImage*
    auto url = static_cast<Qt3DRender::QTextureImage*>(img)->source();
    return url.toLocalFile();
}

QString MaterialProxy::getNormalMap()
{
    Qt3DRender::QAbstractTextureImage* img = nullptr;

    switch(type)
    {
    case MaterialType::NormalDiffuseMap:
        {
            auto mat = static_cast<Qt3DExtras::QNormalDiffuseMapMaterial*>(material);
            auto list = mat->normal()->textureImages();
            if(list.size()>0)
                img = list[0];
        }
        break;
    case MaterialType::Reflection:
        {
            auto mat = static_cast<ReflectiveMaterial*>(material);
            auto list = mat->getNormalTexture()->textureImages();
            if(list.size()>0)
                img = list[0];
        }
        break;
    case MaterialType::Refraction:
        {
            auto mat = static_cast<RefractiveMaterial*>(material);
            auto list = mat->getNormalTexture()->textureImages();
            if(list.size()>0)
                img = list[0];
        }
        break;
    default:
        break;
    }

    if(img==nullptr)
        return "";

    //all images can be safely casted to Qt3DRender::QTextureImage*
    auto url = static_cast<Qt3DRender::QTextureImage*>(img)->source();
    return url.toLocalFile();
}

Qt3DRender::QMaterial* MaterialUtils::createMaterial(MaterialType type)
{
    using namespace Qt3DRender;

    switch(type)
    {
    case MaterialType::Default:
        return new Qt3DExtras::QPhongMaterial();
    case MaterialType::DiffuseMap:
        return new Qt3DExtras::QDiffuseMapMaterial();
    case MaterialType::DiffuseSpecularMap:
        return new Qt3DExtras::QDiffuseSpecularMapMaterial();
    case MaterialType::NormalDiffuseMap:
        return new Qt3DExtras::QNormalDiffuseMapMaterial();
    case MaterialType::Reflection:
        return new ReflectiveMaterial();
    case MaterialType::Refraction:
        return new RefractiveMaterial();
    default:
        return nullptr;//or throw exception?
    }
}
