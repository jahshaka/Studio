/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

in vec3 vertexPosition;
in vec3 vertexNormal;
in vec2 vertexTexCoord;
in vec4 vertexTangent;

out vec3 worldPosition;
out vec2 texCoord;
out mat3 tangentMatrix;
out mat3 tanToWorld;
out vec3 reflVec;
//out vec3 incidentRay;
out vec3 vertNormal;

uniform mat4 modelMatrix;
uniform mat3 modelNormalMatrix;
uniform mat4 mvp;

//world space
uniform vec3 eyePosition;

uniform float texCoordScale;

//for fog
out float zDist;

void main()
{
    texCoord = vertexTexCoord;

    // Transform position, normal, and tangent to world coords
    vec3 normal = normalize( modelNormalMatrix * vertexNormal );
    vertNormal = normal;
    vec3 tangent = normalize( modelNormalMatrix * vertexTangent.xyz );
    worldPosition = vec3( modelMatrix * vec4( vertexPosition, 1.0 ) );

    // Calculate binormal vector
    vec3 binormal = normalize( cross( normal, tangent ) );

    //world to tan
    tangentMatrix = mat3(
            tangent.x, binormal.x, normal.x,
            tangent.y, binormal.y, normal.y,
            tangent.z, binormal.z, normal.z);
    //tan to world
    tanToWorld = mat3(tangent, binormal, normal);

    // Calculate vertex position in clip coordinates
    vec4 clipPos = mvp * vec4( vertexPosition, 1.0 );
    zDist = clipPos.z/clipPos.w;

    gl_Position = clipPos;
}
