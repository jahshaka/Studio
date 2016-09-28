/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALPROXY_H
#define MATERIALPROXY_H

#include <QColor>
#include "../scenegraph/scenenodes.h"

namespace Qt3DRender
{
    class QMaterial;
    class QAbstractTextureProvider;
}
class QColor;
class QString;

//enum class MaterialType;

struct MaterialData
{
    QColor diffuse;
    QColor specular;
    QColor ambient;

    float shininess;
    float alpha;

    QString diffuseTex;
    QString specularTex;
    QString normalTex;

    MaterialData()
    {
        shininess = 0;
        alpha = 1;

        diffuse = QColor::fromRgb(0,0,0);
        specular = QColor::fromRgb(0,0,0);
        ambient = QColor::fromRgb(0,0,0);

        diffuseTex = nullptr;
        specularTex = nullptr;
        normalTex = nullptr;
    }
};

class MaterialProxy
{
    Qt3DRender::QMaterial* material;
    MaterialType type;
public:

    MaterialProxy(Qt3DRender::QMaterial* mat,MaterialType type)
    {
        this->material = mat;
        this->type = type;
    }

    void setData(MaterialData* data);
    MaterialData* getData();

    void setDiffuse(QColor col);
    void setSpecular(QColor col);
    void setAmbient(QColor col);

    void setShininess(float spec);
    void setAlpha(float alpha);

    void setDiffuseMap(QString path);
    void setSpecularMap(QString path);
    void setNormalMap(QString path);

    QColor getDiffuse();
    QColor getSpecular();
    QColor getAmbient();

    float getShininess();
    float getAlpha();

    QString getDiffuseMap();
    QString getSpecularMap();
    QString getNormalMap();
};

class MaterialUtils
{
public:
    static Qt3DRender::QMaterial* createMaterial(MaterialType type);
};

#endif // MATERIALPROXY_H
