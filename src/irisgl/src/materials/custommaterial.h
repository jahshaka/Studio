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
#include <QJsonObject>
#include <QJsonArray>
#include <QOpenGLShaderProgram>
#include <QColor>
#include <vector>
#include <map>

class QOpenGLFunctions_3_2_Core;

namespace iris
{

class CustomMaterial : public Material
{
public:
    QJsonObject jahShaderMaster;
    void begin(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;
    void end(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;

    void generate(const QJsonObject &jahShader);

    void setTextureWithUniform(QString, QString);
    void updateTextureAndToggleUniform(int, QString);
    void updateFloatAndUniform(int, float);
    void updateColorAndUniform(int, QColor);
    QJsonObject getShaderFile() const;
    void purge();
    void setMaterialName(const QString&);
    void setBaseMaterialProperties(const QJsonObject&);
    QString getMaterialName() const;
    int getCalculatedPropHeight() const;

    std::vector<MatStruct<bool>>    boolUniforms;
    std::vector<MatStruct<float>>   sliderUniforms;
    std::vector<MatStruct<QColor>>  colorUniforms;
    std::vector<MatStruct<QString>> textureUniforms;
    std::vector<MatStruct<bool>>    textureToggleUniforms;

    static CustomMaterialPtr create();

private:
    CustomMaterial();

    int finalSize;
    QString materialName;
    Texture2DPtr intermediateTexture;
};

}

#endif // CUSTOMMATERIAL_H
