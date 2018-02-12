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

uniform mat4 u_lightSpaceMatrix;
uniform mat4 u_worldMatrix;

void main() {
    gl_Position = u_lightSpaceMatrix * u_worldMatrix * vec4(a_pos, 1.0);
}
