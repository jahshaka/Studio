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
in vec3 a_normal;

uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;
uniform mat3 u_normalMatrix;
uniform float u_textureScale;
uniform vec3 u_eyePos;

out vec2 v_texCoord;
out vec3 v_viewNormal;
out vec3 v_viewEyeDir;
out vec3 v_worldPos;

void main() {
    v_worldPos = (u_worldMatrix * vec4(a_pos, 1.0)).xyz;
    gl_Position = u_projMatrix * u_viewMatrix * u_worldMatrix * vec4(a_pos, 1.0);

    v_texCoord = a_texCoord * u_textureScale;
    vec3 eyeDir = normalize(v_worldPos - u_eyePos);
    v_viewEyeDir = mat3(u_viewMatrix) * eyeDir;

    vec3 normal = u_normalMatrix * a_normal;
    v_viewNormal = normalize(mat3(u_viewMatrix) * normal);
}
