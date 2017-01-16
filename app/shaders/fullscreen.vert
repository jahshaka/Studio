/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

attribute vec3 a_pos;
attribute vec2 a_texCoord;

varying vec2 v_texCoord;

void main()
{
        v_texCoord = a_texCoord*vec2(1,-1);
        gl_Position = vec4(a_pos,1);
}
