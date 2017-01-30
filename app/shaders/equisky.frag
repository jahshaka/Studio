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

//uniform mat4 uInverseViewMatrix;
uniform sampler2D tex;

//in vec2 texCoord;
in vec3 worldNormal;

out vec4 fragColor;

/*
vec2 envMapEquirect(vec3 wcNormal, float flipEnvMap) {
  //I assume envMap texture has been flipped the WebGL way (pixel 0,0 is a the bottom)
  //therefore we flip wcNorma.y as acos(1) = 0
  float phi = acos(wcNormal.y);
  float theta = atan(flipEnvMap * wcNormal.x, wcNormal.z) + PI;
  return vec2(theta / PI2, phi / PI);
}
*/

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
    //reflection vector in the world space. We negate wcEyeDir as the reflect function expect incident vector pointing towards the surface
    //vec3 reflectionWorld = reflect(-wcEyeDir, normalize(worldNormal));

    //fragColor = textureCube(tex, envMapEquirect(reflectionWorld));
    fragColor = texture(tex, envMapEquirect(normalize(worldNormal)));
}
