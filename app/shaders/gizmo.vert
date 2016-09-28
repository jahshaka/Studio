/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

in vec3 vertexPosition;
in vec3 vertexNormal;

out vec3 normal;
out float originDist;
out float vecDist;

uniform mat4 mvp;
uniform mat3 modelNormalMatrix;
uniform mat4 modelView;


void main()
{
    normal = normalize(modelNormalMatrix*vertexNormal);
    gl_Position = mvp * vec4( vertexPosition, 1.0 );

    //object's center
    vec4 originVec = modelView*vec4(0,0,0,1);
    vec4 viewVec = modelView*vec4(vertexPosition,1.0);
     vecDist = length(viewVec.xyz);
     originDist = length(originVec.xyz);
}
