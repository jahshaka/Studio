/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150

uniform sampler2D tex;
uniform bool useTexture;
uniform vec4 color;

in vec2 v_texCoord;

out vec4 fragColor;

void main()
{
    vec4 color = texture( tex, v_texCoord ).rgba;

    //color.a = 1.0;
    fragColor = color;
}
