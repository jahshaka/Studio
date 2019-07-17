/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

#define PI2 6.28318530718
#define RECIPROCAL_PI2 0.15915494

uniform vec4 color2;
out vec4 fragColor;

in vec3 v_pos;
in vec3 v_normal;

uniform vec3 u_eyePos;
uniform sampler2D u_skyTexture;

const float air = 1.0;
const float glass = 1.51714;

const float Eta = air / glass;
const float R0 = ((air - glass) * (air - glass)) / ((air + glass) * (air + glass));

uniform float u_refractive_factor;
uniform float u_influence;
uniform float u_alpha;

vec2 envMapEquirect(vec3 wcNormal, float flipEnvMap) {
    float y = clamp( -1.0 * wcNormal.y * 0.5 + 0.5,0,1 );
    float x = atan(  wcNormal.z, wcNormal.x ) * RECIPROCAL_PI2 + 0.5;
    return vec2(x, y);
}

void main()
{
    vec3 I = normalize(v_pos - u_eyePos);
    // vec3 R = refract(I, normalize(v_normal), u_refrac_strength);

    vec3 v_refraction = refract(I, normalize(v_normal), u_refractive_factor);
    vec3 v_reflection = reflect(I, normalize(v_normal));

    // float v_fresnel = R0 + (1.0 - R0) * pow((1.0 - dot(-I, normalize(v_normal))), 5.0);

    vec4 refr_color = texture(u_skyTexture, envMapEquirect(normalize(v_refraction), -1.0));
    vec4 refl_color = texture(u_skyTexture, envMapEquirect(normalize(v_reflection), -1.0));

    vec4 mixedTr = mix(refr_color, refl_color, u_influence);

    fragColor = vec4(mixedTr.rgb, u_alpha);
}
