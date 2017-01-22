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
out vec3 reflVec;
//out vec3 incidentRay;
out vec3 vertNormal;

uniform mat4 modelMatrix;
uniform mat3 modelNormalMatrix;
uniform mat4 mvp;

//world space
uniform vec3 eyePosition;

uniform float texCoordScale;

vec3 reflect(vec3 I, vec3 N) {
    return I - 2.0 * dot(N, I) * N;
}

void main()
{
    // Pass through texture coordinates
    texCoord = vertexTexCoord;

    // Transform position, normal, and tangent to world coords
    vec3 normal = normalize( modelNormalMatrix * vertexNormal );
    vertNormal = normal;
    vec3 tangent = normalize( modelNormalMatrix * vertexTangent.xyz );
    worldPosition = vec3( modelMatrix * vec4( vertexPosition, 1.0 ) );

    // Calculate binormal vector
    vec3 binormal = normalize( cross( normal, tangent ) );

    // Construct matrix to transform from eye coords to tangent space
    /*
    tangentMatrix = mat3 (
                tangent.x, binormal.x, normal.x,
                tangent.y, binormal.y, normal.y,
                tangent.z, binormal.z, normal.z );
                */
    tangentMatrix = mat3 (tangent,binormal,normal);

    // Calculate vertex position in clip coordinates
    gl_Position = mvp * vec4( vertexPosition, 1.0 );

    //vec3 incidentRay = normalize(worldPosition-eyePosition);
    //vec3 i = normalize(worldPosition-eyePosition);
    //reflVec = reflect(i,normal);
}
