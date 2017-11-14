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
in vec2 a_texCoord;

out vec2 v_texCoord;

uniform bool flipY;
uniform mat4 matrix;

void main()
{
    if(flipY)
        v_texCoord = a_texCoord * vec2(1,-1);
    else
        v_texCoord = a_texCoord;

    gl_Position = matrix * vec4(a_pos,1);
    //gl_Position = vec4(a_pos,1);
}
