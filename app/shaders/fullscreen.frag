/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

varying vec2 v_texCoord;

uniform sampler2D texture;

void main()
{
	vec4 col = texture2D(texture,v_texCoord);

    gl_FragColor = col;
}
