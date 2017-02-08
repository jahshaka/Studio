#ifndef MATERIALHELPER_H
#define MATERIALHELPER_H

#include "../irisglfwd.h"

class aiMaterial;

namespace iris
{

class MaterialHelper
{
public:
    static DefaultMaterialPtr createMaterial(aiMaterial* aiMat, QString assetPath);

};

}

#endif // MATERIALHELPER_H
