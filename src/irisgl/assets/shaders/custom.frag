/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

uniform vec4 color;
out vec4 fragColor;

in vec3 v_pos;
in vec3 v_normal;

uniform vec3 u_eyePos;
uniform samplerCube skybox;

void main()
{
    float ratio = 1.00 / 1.52;
    vec3 I = normalize(v_pos - u_eyePos);
    vec3 R = refract(I, normalize(v_normal), ratio);
    fragColor = texture(skybox, R); //vec4(0, 1, 0, 1);
}
