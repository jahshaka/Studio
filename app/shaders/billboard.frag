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

varying vec2 v_texCoord;

void main()
{
    /*
    if(useTexture)
    {
        vec4 color = texture( tex, v_texCoord ).rgba;
        if(color.a<0.1f)
            discard;

        color.a = 1.0;
        gl_FragColor = color;
    }
    else
        gl_FragColor = color;
    */

    vec4 color = texture( tex, v_texCoord ).rgba;
    if(color.a<0.1f)
        discard;

    color.a = 1.0;
    gl_FragColor = color;
    //gl_FragColor = vec4(1.0,1.0,1.0,1.0);
}
