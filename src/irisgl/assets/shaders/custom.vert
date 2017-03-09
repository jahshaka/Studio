/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

in vec3 a_pos;
in vec3 a_normal;
in vec2 a_texCoord;

out vec3 v_normal;
out vec2 v_texCoord;

uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;

void main()
{
    v_normal = normalize(u_worldMatrix * vec4(a_normal, 0.0)).xyz;
    v_texCoord = a_texCoord;

    gl_Position = u_projMatrix * u_viewMatrix * u_worldMatrix * vec4(a_pos, 1.0);
}
