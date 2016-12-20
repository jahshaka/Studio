/**************************************************************************
This file is part of IrisGL
http://www.irisgl.org
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#ifndef SHADERPROGRAM_H
#define SHADERPROGRAM_H

#include "../irisglfwd.h"

namespace iris
{

class Shader
{
public:
    static ShaderPtr load(QString vertexShaderFile,QString fragmentShaderFile);
    static ShaderPtr create(QString vertexShader,QString fragmentShader);
};

}

#endif // SHADERPROGRAM_H
