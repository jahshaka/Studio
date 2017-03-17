/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

out vec4 fragColor;

uniform vec4 color;

in vec3 v_normal;
in vec2 v_texCoord;

uniform sampler2D tex;

void main()
{
    vec4 ambient = vec4(0.9);
    vec3 lightVec = -vec3(0, -1, 0); //shining down
    float light = max(dot(v_normal, lightVec), 0.0);

    vec4 texColor = texture(tex,v_texCoord);

    fragColor = vec4(0, 1, 1, 1); //texColor * (ambient + light * (1.0 - ambient));
}
