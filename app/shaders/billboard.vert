/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150

in vec3 a_pos;
in vec2 a_texCoord;

out vec2 v_texCoord;

uniform mat4 u_worldMatrix;
uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;

void main()
{
    v_texCoord = a_texCoord;

    mat4 mat = u_viewMatrix*u_worldMatrix;


    mat[0][0] = 1.0f;
    mat[0][1] = 0.0f;
    mat[0][2] = 0.0f;

    mat[1][0] = 0.0f;
    mat[1][1] = 1.0f;
    mat[1][2] = 0.0f;

    mat[2][0] = 0.0f;
    mat[2][1] = 0.0f;
    mat[2][2] = 1.0f;


    gl_Position = u_projMatrix * mat * vec4( a_pos, 1.0 );
    //gl_Position =  vec4( a_pos, 1.0 );
    //gl_Position =  u_projMatrix * u_viewMatrix * u_worldMatrix * vec4( a_pos, 1.0 );
    //gl_Position = u_projMatrix*u_viewMatrix*u_worldMatrix*vec4(a_pos,1.0);
}
