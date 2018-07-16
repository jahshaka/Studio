/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

uniform vec4 color;
out vec4 fragColor;

in vec4 v_color;

void main()
{
    fragColor = v_color * color;
    gl_FragDepth = gl_FragCoord.z -0.0001;
}
