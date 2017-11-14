/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

#define PI 3.14159265359
#define PI2 6.28318530718
#define RECIPROCAL_PI2 0.15915494

//uniform sampler2D tex;
uniform samplerCube cubemap;
uniform bool useTexture;
uniform vec4 color;

in vec3 v_worldNormal;
in vec3 v_texCoord;
out vec4 fragColor;

void main()
{
     // vec4(1, 1, .5, 1);
    if(useTexture)
//        fragColor = color*texture(tex, envMapEquirect(normalize(v_worldNormal)));
        fragColor = texture(cubemap, v_worldNormal);
    else
        fragColor = color;
    //gl_FragColor = vec4(1,1,1,1);
}
