/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

in vec2 v_texCoord;

uniform sampler2D tex;
out vec4 fragColor;

void main()
{
        vec4 col = texture(tex,v_texCoord);

    fragColor = col;
}
