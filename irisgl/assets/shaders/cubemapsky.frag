/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

uniform samplerCube cubemap;

in vec3 v_worldNormal;

out vec4 fragColor;

void main()
{
    fragColor = texture(cubemap, v_worldNormal);
}