#ifndef SHAPEBUILDER_H
#define SHAPEBUILDER_H

#include "../../irisglfwd.h"

namespace iris
{

class ShapeHelper
{
public:

    MeshPtr createWireCube();
    MeshPtr createWireSphere();
};

}

#endif // SHAPEBUILDER_H
