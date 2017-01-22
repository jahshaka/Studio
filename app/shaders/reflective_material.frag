/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

#define PI 3.14159265359
#define PI2 6.28318530718
#define RECIPROCAL_PI2 0.15915494

in vec3 worldPosition;
in vec2 texCoord;
in mat3 tangentMatrix;
in vec3 vertNormal;

uniform sampler2D diffuseTexture;
uniform sampler2D reflTexture;
uniform sampler2D normalTexture;

uniform vec3 eyePosition;

out vec4 fragColor;

vec2 envMapEquirect(vec3 wcNormal, float flipEnvMap) {
    float y = clamp( -1.0 * wcNormal.y * 0.5 + 0.5,0,1 );
    float x = atan(  wcNormal.z, wcNormal.x ) * RECIPROCAL_PI2 + 0.5;
    return vec2(x, y);
}

vec2 envMapEquirect(vec3 wcNormal) {
    //-1.0 for left handed coordinate system oriented texture (usual case)
    return envMapEquirect(wcNormal, -1.0);
}


void main()
{
    vec3 normal;
    vec3 texNorm = texture( normalTexture, texCoord ).rgb;
    if(length(texNorm)<=0.000001)
        normal = vertNormal;
    else
    {
        normal = 2.0 * texNorm - vec3( 1.0 );
        normal = tangentMatrix*normal;
    }

    vec3 incidentRay = normalize(worldPosition-eyePosition);
    vec3 reflVec = reflect(incidentRay,normal);

    fragColor = texture2D(reflTexture, envMapEquirect(normalize(reflVec)));

}
