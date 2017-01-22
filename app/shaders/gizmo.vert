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

out vec3 v_normal;
out float v_originDist;
out float v_dist;

uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;

void main()
{
    v_normal = normalize(u_worldMatrix * vec4(a_normal, 0.0)).xyz;
    gl_Position = u_projMatrix * u_viewMatrix * u_worldMatrix * vec4(a_pos, 1.0);

    vec4 originVec = u_viewMatrix * u_worldMatrix * vec4(0, 0, 0, 1);
    vec4 viewVec = u_viewMatrix * u_worldMatrix * vec4(a_pos, 1.0);

    v_dist = length(viewVec.xyz);
    v_originDist = length(originVec.xyz);
}
