/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/
#ifndef MATERIALPRESETS_H
#define MATERIALPRESETS_H

#include <QString>
#include <QColor>

struct MaterialPreset
{
    QString name;
    QString icon;

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


    static QList<MaterialPreset> getDefaultPresets();
};

#endif // MATERIALPRESETS_H
