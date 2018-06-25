/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

//in vec3 vertexPosition;
in vec3 a_pos;
in vec3 a_normal;

uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;
uniform mat3 u_normalMatrix;

out vec3 viewDir;
out vec3 v_normal;
out vec3 v_worldPos;


void main()
{
    //gl_Position = mvp * vec4( a_pos, 1.0 );
    gl_Position = u_projMatrix*u_viewMatrix*u_worldMatrix*vec4(a_pos,1.0);
    v_normal = normalize((u_normalMatrix*a_normal));
    v_worldPos = (u_worldMatrix*vec4(a_pos,1.0)).xyz;
}
