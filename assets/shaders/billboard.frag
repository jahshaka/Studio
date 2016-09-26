/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Karsten Becker <jahshaka@gmail.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

uniform sampler2D tex;
uniform bool useTexture;
uniform vec4 color;

in vec2 texCoord;

out vec4 fragColor;

void main()
{
    if(useTexture)
    {
        vec4 color = texture( tex, texCoord ).rgba;
        if(color.a<0.1f)
            discard;
        color.a = 1.0;
        fragColor = color;
    }
    else
        fragColor = color;

    //fragColor = vec4(1.0,0.0,0.0,1);
    //fragColor = vec4(1,1,1,1);
}
