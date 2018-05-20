#ifndef LINECOLORMATERIAL_H
#define LINECOLORMATERIAL_H

#include "../irisglfwd.h"
#include "../graphics/material.h"
#include <QColor>

class QOpenGLFunctions_3_2_Core;
namespace iris
{

class LineColorMaterial;
typedef QSharedPointer<LineColorMaterial> LineColorMaterialPtr;

class LineColorMaterial : public Material
{
public:
    QColor color;
    QColor getColor() const;
    void setColor(const QColor &value);

    void begin(GraphicsDevicePtr device, ScenePtr scene) override;
    void end(GraphicsDevicePtr device, ScenePtr scene) override;

    static LineColorMaterialPtr create()
    {
        return LineColorMaterialPtr(new LineColorMaterial());
    }

private:
    LineColorMaterial();
};



}

#endif // LINECOLORMATERIAL_H
