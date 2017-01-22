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

out vec3 worldNormal;

uniform mat4 mvp;
uniform mat3 modelViewNormal;
uniform mat4 modelMatrix;

vec3 reflect(vec3 I, vec3 N) {
    return I - 2.0 * dot(N, I) * N;
}

void main()
{
    //worldNormal = normalize(modelViewNormal*vertexNormal);
    worldNormal = normalize(vertexPosition);

    gl_Position = mvp * vec4( vertexPosition, 1.0 );
}
