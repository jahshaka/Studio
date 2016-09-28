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
in vec2 vertexTexCoord;

out vec2 texCoord;

uniform mat4 modelView;
uniform mat4 projectionMatrix;
uniform mat4 mvp;

void main()
{
    texCoord = vertexTexCoord;

    mat4 mat = modelView;

    mat[0][0] = 1.0f;
    mat[0][1] = 0.0f;
    mat[0][2] = 0.0f;

    mat[1][0] = 0.0f;
    mat[1][1] = 1.0f;
    mat[1][2] = 0.0f;

    mat[2][0] = 0.0f;
    mat[2][1] = 0.0f;
    mat[2][2] = 1.0f;

    gl_Position = projectionMatrix * mat * vec4( vertexPosition, 1.0 );
    //gl_Position = projectionMatrix * modelView * vec4( vertexPosition, 1.0 );
    //gl_Position = mvp * vec4( vertexPosition, 1.0 );
}
