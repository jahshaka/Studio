#ifndef SKYMATERIAL_H
#define SKYMATERIAL_H


#include "../graphics/material.h"
#include "../graphics/texture2d.h"
#include <QOpenGLShaderProgram>
#include <QColor>

class QOpenGLFunctions_3_2_Core;

namespace iris
{

class Texture;
class Texture2D;
class DefaultSkyMaterial;
typedef QSharedPointer<DefaultSkyMaterial> DefaultSkyMaterialPtr;
typedef QSharedPointer<Texture2D> Texture2DPtr;

/**
 * This is the default sky material.
 * It's parameters are:
 * skyTexture
 * skyColor
 *
 * if a skyTexture is specified then the final output color is the
 * texture multiplied by the color. Else, only the color is used.
 */
class DefaultSkyMaterial:public Material
{
public:


    void setSkyTexture(Texture2DPtr tex);
    void clearSkyTexture();
    Texture2DPtr getSkyTexture();

    void setSkyColor(QColor color);
    QColor getSkyColor();

    void begin(QOpenGLFunctions_3_2_Core* gl) override;
    void end(QOpenGLFunctions_3_2_Core* gl) override;

    static DefaultSkyMaterialPtr create();
private:
    DefaultSkyMaterial();

    Texture2DPtr texture;
    QColor color;
};

}

#endif // SKYMATERIAL_H
