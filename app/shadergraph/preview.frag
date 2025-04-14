/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

//http://www.tomdalling.com/blog/modern-opengl/07-more-lighting-ambient-specular-attenuation-gamma/

#version 150

#define PI 3.14159265359
#define PI2 6.28318530718
#define RECIPROCAL_PI2 0.15915494

in vec2 v_texCoord;
in vec3 v_localNormal;
in vec3 v_normal;
in vec3 v_worldPos;
in mat3 v_tanToWorld;

uniform vec3 u_eyePos;
uniform vec3 u_sceneAmbient;
uniform float u_time = 0.0f;


out vec4 fragColor;

struct PreviewMaterial
{
	vec4 color;
};

void preview(inout PreviewMaterial preview);

void main()
{
	PreviewMaterial data;
	data.color = vec4(0.0, 0.0, 0.0, 1.0);
    preview(data);

    fragColor = data.color;
}
