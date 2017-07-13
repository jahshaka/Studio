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
in vec4 a_boneWeights;
in vec4 a_boneIndices;

uniform mat4 matrix;
uniform mat4 u_worldMatrix;

const int MAX_BONES = 100;
uniform mat4 u_bones[MAX_BONES];
uniform bool u_hasBones;

uniform mat4 u_lightSpaceMatrix;

void main()
{
    mat4 boneMatrix = u_bones[int(a_boneIndices[0])] * a_boneWeights[0];
    boneMatrix += u_bones[int(a_boneIndices[1])] * a_boneWeights[1];
    boneMatrix += u_bones[int(a_boneIndices[2])] * a_boneWeights[2];
    boneMatrix += u_bones[int(a_boneIndices[3])] * a_boneWeights[3];

    gl_Position = u_lightSpaceMatrix*u_worldMatrix*boneMatrix*vec4(a_pos,1.0);
}
