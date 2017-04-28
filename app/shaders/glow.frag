/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

in vec2 v_texCoord;
in vec3 v_normal;
in vec3 v_worldPos;
in mat3 v_tanToWorld;
uniform vec3 u_eyePos;
uniform float u_intensity;

uniform vec3 u_color;
out vec4 fragColor;

// normal should be in world space
float Fresnel(vec3 normal, float power)
{
    vec3 I = normalize(u_eyePos-v_worldPos);
    return pow(dot(normal, I), power);
}

void main()
{
    float alpha = pow(Fresnel(-v_normal, 1.0), 1) * u_intensity;
    fragColor = vec4(u_color, alpha);
}
