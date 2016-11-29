#ifndef SKYMATERIAL_H
#define SKYMATERIAL_H


#include "../graphics/material.h"
#include "../graphics/texture2d.h"
#include <QOpenGLShaderProgram>
#include <QColor>
#include

class QOpenGLFunctions_3_2_Core;

namespace jah3d
{

class Texture;
class Texture2D;

/**
 * This is the default sky material.
 * It's parameters are:
 * skyTexture
 * skyColor
 *
 * if a skyTexture is specified,then the final output color is the
 * texture multiplied by the color. Else, only the color is used.
 */
class SkyMaterial:public Material
{
public:
    SkyMaterial();

    void setSkyTexture(Texture2DPtr tex);
    void clearSkyTexture();

    void setSkyColor(QColor color);
    QColor getSkyColor();
};

}

#endif // SKYMATERIAL_H
