/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

in vec3 a_pos;
// in vec2 a_texCoord;
//attribute vec3 a_normal;

uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;

out vec3 v_worldNormal;
out vec3 v_texCoord;

void main()
{
    v_worldNormal = normalize(a_pos);

    vec4 finalPos = u_projMatrix * u_viewMatrix * vec4( a_pos*1000, 1.0 );

    v_texCoord = a_pos;

    //rendering trick to place sky behind all other objects
    //http://www.haroldserrano.com/blog/how-to-apply-a-skybox-in-opengl
    //https://www.opengl.org/discussion_boards/showthread.php/171867-skybox-problem?p=1206427&viewfull=1#post1206427
    finalPos.z = finalPos.w-0.1f;
    gl_Position = finalPos.xyzw;
}
