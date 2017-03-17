/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

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
