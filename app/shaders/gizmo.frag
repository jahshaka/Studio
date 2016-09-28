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

in vec3 normal;
in float originDist;
in float vecDist;

uniform bool showHalf;
void main()
{
    //if(alpha==0)
    //    discard;

    vec4 ambient = vec4(0.1);
    vec3 lightVec = -vec3(0,-1,0);//shining down
    float light = max(dot(normal,lightVec),0.0);

    float alpha = 1;
    if(showHalf)
        alpha = clamp(((originDist+0.1)-vecDist)/((originDist+0.1)-originDist),0.0,1.0);

    fragColor = color*(ambient+light*(1.0-ambient),alpha);
}
