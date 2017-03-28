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

    void setTexture(Texture2DPtr tex);
    Texture2DPtr getTexture();

    QJsonObject jahShaderMaster;
    QJsonObject getShaderFile() const {
        return jahShaderMaster;
    }

    void begin(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;
    void end(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;

    void generate(const QJsonObject &jahShader);

    std::vector<float> sliderValues;
    std::vector<QString> uniformName;

    std::vector<QColor> colorValue;
    std::vector<QString> colorUniform;

    std::vector<QString> textureName;
    std::vector<QString> textureUniform;

    // the new structures
//    std::map<QString, QString> colorUniforms;
//    std::map<QString, float> sliderUniforms;
    std::map<QString, bool> boolUniforms;
//    std::map<QString, QString> textureUniforms;
//    std::map<QString, bool> textureToggleUniforms;

    std::vector<MatStruct<float>> sliderUniforms;
    std::vector<MatStruct<QColor>> colorUniforms;
    std::vector<MatStruct<QString>> textureUniforms;
    std::vector<MatStruct<bool>> textureToggleUniforms;

    QString name;

    void setTextureWithUniform(QString, QString);
    void updateTextureAndToggleUniform(int, QString);

    static CustomMaterialPtr create();

private:
    CustomMaterial();
    Texture2DPtr intermediateTexture;
};

}

#endif // CUSTOMMATERIAL_H
