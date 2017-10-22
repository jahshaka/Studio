#ifndef SHAPEBUILDER_H
#define SHAPEBUILDER_H

#include "../../irisglfwd.h"

namespace iris
{

class ShapeHelper
{
public:

    static MeshPtr createWireCube(float size = 1);
    static MeshPtr createWireSphere(float radius = 0.5);
    static MeshPtr createWireCone(float baseRadius = 0.5);
};

}

#endif // SHAPEBUILDER_H
