#ifndef MATERIAL_H
#define MATERIAL_H

#include "shaderprogram.h"

namespace jah3d
{

typedef QSharedPointer<Material> MaterialPtr;

class Material
{
public:
    ShaderProgramPtr program;
    void begin();
    void end();
};

}

#endif // MATERIAL_H
