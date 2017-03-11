#ifndef CUSTOMMATERIAL_H
#define CUSTOMMATERIAL_H

#include "../graphics/material.h"
#include "../irisglfwd.h"
#include <QJsonObject>
#include <QOpenGLShaderProgram>
#include <QColor>
#include <vector>

class QOpenGLFunctions_3_2_Core;

namespace iris
{

class CustomMaterial : public Material
{
public:

    void setTexture(Texture2DPtr tex);
    Texture2DPtr getTexture();

    void begin(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;
    void end(QOpenGLFunctions_3_2_Core* gl, ScenePtr scene) override;

    float sliderValues[12];
    float uniformValue[12];
    QString uniformName[12];
    int allocated;
    void initializeDefaultValues(const QJsonObject &jahShader);

    static CustomMaterialPtr create();

private:
    CustomMaterial();

    Texture2DPtr texture;
};

}

#endif // CUSTOMMATERIAL_H
