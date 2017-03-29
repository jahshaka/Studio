/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef MATERIALPRESET_H
#define MATERIALPRESET_H

#include <QColor>
#include <QVector3D>

struct MaterialPreset
{
    QString name;
    QString icon;
    QString type;

    QColor ambientColor;

    QColor diffuseColor;
    QString diffuseTexture;

    QColor specularColor;
    QString specularTexture;
    float shininess;

    QString normalTexture;
    float normalIntensity;

    QString reflectionTexture;
    float reflectionInfluence;

    float textureScale;
};

#endif // MATERIALPRESET_H
