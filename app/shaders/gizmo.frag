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
in float v_originDist;
in float v_dist;

uniform bool showHalf;

void main()
{
    vec4 ambient = vec4(0.1);
    vec3 lightVec = -vec3(0, -1, 0); //shining down
    float light = max(dot(v_normal, lightVec), 0.0);

    float alpha = 1;

    if (showHalf) {
        alpha = clamp(((v_originDist + 0.1) - v_dist) / ((v_originDist + 0.1) - v_originDist), 0.0, 1.0);
    }

    fragColor = color * (ambient + light * (1.0 - ambient), alpha);
}
