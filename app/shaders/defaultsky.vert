/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

attribute vec3 a_pos;
//attribute vec2 a_texCoord;
//attribute vec3 a_normal;

uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;

varying vec3 v_worldNormal;

vec3 reflect(vec3 I, vec3 N) {
    return I - 2.0 * dot(N, I) * N;
}

void main()
{
    v_worldNormal = normalize(a_pos);

    gl_Position = u_projMatrix * u_projMatrix * u_worldMatrix * vec4( a_pos, 1.0 );
}
