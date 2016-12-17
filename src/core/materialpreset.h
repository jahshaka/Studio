#ifndef MATERIALPRESET_H
#define MATERIALPRESET_H

#include <QColor>
#include <QVector3D>

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
};

#endif // MATERIALPRESET_H
