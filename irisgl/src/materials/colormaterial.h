#ifndef COLORMATERIAL_H
#define COLORMATERIAL_H

#include "../irisglfwd.h"
#include "../graphics/material.h"
#include <QColor>

class QOpenGLFunctions_3_2_Core;
namespace iris
{

class ColorMaterial;
typedef QSharedPointer<ColorMaterial> ColorMaterialPtr;

class ColorMaterial : public Material
{
public:
    QColor color;
    QColor getColor() const;
    void setColor(const QColor &value);

    void begin(GraphicsDevicePtr device, ScenePtr scene) override;
    void end(GraphicsDevicePtr device, ScenePtr scene) override;

    static ColorMaterialPtr create()
    {
        return ColorMaterialPtr(new ColorMaterial());
    }

private:
    ColorMaterial();
};



}

#endif // COLORMATERIAL_H
