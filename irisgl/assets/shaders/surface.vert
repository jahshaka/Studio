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
    v_worldPos = (u_worldMatrix*vec4(a_pos,1.0)).xyz;
    //gl_Position = matrix*vec4(a_pos,1.0);
    gl_Position = u_projMatrix*u_viewMatrix*u_worldMatrix*vec4(a_pos,1.0);

    v_texCoord = a_texCoord;
    //v_texCoord = a_texCoord*2;

    v_normal = normalize((u_normalMatrix*a_normal));
    vec3 v_tangent = normalize((u_normalMatrix*a_tangent));
    //vec3 v_bitangent = cross(v_normal,v_tangent);
    vec3 v_bitangent = cross(v_tangent,v_normal);

    FragPosLightSpace = u_lightSpaceMatrix * vec4(v_worldPos, 1.0);

    /*
      //actual world to tangent space
    v_tanToWorld = mat3(v_tangent[0],v_bitangent[0],v_normal[0],
                        v_tangent[1],v_bitangent[1],v_normal[1],
                        v_tangent[2],v_bitangent[2],v_normal[2]);
                        */

    v_tanToWorld = mat3(v_tangent,v_bitangent,v_normal);
}
