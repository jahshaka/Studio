#ifndef SHAPEBUILDER_H
#define SHAPEBUILDER_H

#include "../../irisglfwd.h"

namespace iris
{

class ShapeHelper
{
public:

    static MeshPtr createWireCube();
    static MeshPtr createWireSphere();
};

}

#endif // SHAPEBUILDER_H
