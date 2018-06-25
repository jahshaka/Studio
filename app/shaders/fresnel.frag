/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

uniform vec4 u_color;
out vec4 fragColor;

uniform vec3 u_eyePos;
uniform float u_fresnelPow;

in vec3 v_normal;
in vec3 v_worldPos;

float fresnel(vec3 normal, float power)
{
    vec3 I = normalize(u_eyePos-v_worldPos);
    return pow(1.0 - dot(normal, I), power);
}

void main()
{
    //fragColor = color;
    //fragColor.rgb = color.rgb;
    //fragColor.a = dot(viewDir, vec3(0, 0, 1));
    //fragColor.rgb = vec3(dot(viewDir, vec3(0, 0, 1)));
    fragColor.rgb = u_color.rgb;
    fragColor.a = fresnel(v_normal, u_fresnelPow);
}
