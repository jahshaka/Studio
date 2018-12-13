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
in vec3 a_tangent;

#ifdef SKINNING_ENABLED
const int MAX_BONES = 100;
uniform mat4 u_bones[MAX_BONES];
in vec4 a_boneWeights;
in vec4 a_boneIndices;
#endif

uniform mat4 matrix;
uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;
uniform mat3 u_normalMatrix;
uniform float u_textureScale;

uniform mat4 u_lightSpaceMatrix;
out vec4 FragPosLightSpace;

out vec2 v_texCoord;
out vec3 v_normal;
out vec3 v_worldPos;
out mat3 v_tanToWorld;//transforms from tangent space to world space

void main()
{
#ifdef SKINNING_ENABLED
    mat4 boneMatrix = u_bones[int(a_boneIndices[0])] * a_boneWeights[0];
    boneMatrix += u_bones[int(a_boneIndices[1])] * a_boneWeights[1];
    boneMatrix += u_bones[int(a_boneIndices[2])] * a_boneWeights[2];
    boneMatrix += u_bones[int(a_boneIndices[3])] * a_boneWeights[3];

    v_worldPos = (u_worldMatrix*boneMatrix*vec4(a_pos,1.0)).xyz;
    gl_Position = u_projMatrix*u_viewMatrix*u_worldMatrix*boneMatrix*vec4(a_pos,1.0);

    v_texCoord = a_texCoord*u_textureScale;

    vec3 skinnedNormal = (boneMatrix * vec4(a_normal,0)).xyz;
    v_normal = normalize((u_normalMatrix*skinnedNormal));
    vec3 v_tangent = normalize((u_normalMatrix*a_tangent));
    vec3 v_bitangent = cross(v_tangent,v_normal);

    FragPosLightSpace = u_lightSpaceMatrix * vec4(v_worldPos, 1.0);

    v_tanToWorld = mat3(v_tangent,v_bitangent,v_normal);

#else

    v_worldPos = (u_worldMatrix*vec4(a_pos,1.0)).xyz;
    gl_Position = u_projMatrix*u_viewMatrix*u_worldMatrix*vec4(a_pos,1.0);

    v_texCoord = a_texCoord*u_textureScale;

    v_normal = normalize((u_normalMatrix*a_normal));
    vec3 v_tangent = normalize((u_normalMatrix*a_tangent));
    vec3 v_bitangent = cross(v_tangent,v_normal);

    v_tanToWorld = mat3(v_tangent,v_bitangent,v_normal);
    
#endif
}
