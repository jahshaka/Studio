/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef CUSTOMMATERIAL_H
#define CUSTOMMATERIAL_H

#include "../graphics/material.h"
#include "../irisglfwd.h"
#include "propertytype.h"

class QOpenGLFunctions_3_2_Core;

namespace iris
{

class CustomMaterial : public Material
{
public:
    QList<Property*> properties;

    void begin(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;
    void end(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;

    void generate(const QString&);
    void setTextureWithUniform(const QString&, const QString&);
    void setValue(const QString&, const QVariant&);
    void setBaseMaterialProperties(const QJsonObject&);
    void setName(const QString&);
    void setProperties(QList<Property*> props);
    void setUniformValues(Property*);
    void purge();

    QString getName() const;
    QString firstTextureSlot() const;
    int getCalculatedPropHeight() const;

    static CustomMaterialPtr create();

private:
    CustomMaterial() = default;
    QString materialName;

    QJsonObject loadShaderFromDisk(const QString &);
};

}

#endif // CUSTOMMATERIAL_H
