/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

in vec4 vertexPosition;

out vec2 texCoord;
out vec3 worldPosition;

uniform mat4 mvp;
uniform mat4 modelMatrix;

uniform float texScale;

void main()
{
    //float texScale = 0.05f;
    worldPosition = (modelMatrix * vec4(vertexPosition.xyz,1.0)).xyz;
    //vec4 pos = mvp * vertexPosition;
    texCoord = worldPosition.xz*texScale;

    vec4 pos = mvp * vec4(vertexPosition.xyz,1);
    gl_Position = pos;
}
