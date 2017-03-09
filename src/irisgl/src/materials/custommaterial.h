#ifndef CUSTOMMATERIAL_H
#define CUSTOMMATERIAL_H

#include "../graphics/material.h"
#include "../irisglfwd.h"
#include <QOpenGLShaderProgram>
#include <QColor>

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

    static CustomMaterialPtr create();
private:
    CustomMaterial();

    Texture2DPtr texture;
};

}

#endif // CUSTOMMATERIAL_H
