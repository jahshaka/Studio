/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#pragma include <surface.frag>

uniform vec3 u_color;
uniform bool u_useDiffuseTex;
uniform sampler2D u_diffuseTex;
uniform float u_fresnelPow;
uniform vec3 u_edgeColor;

void surface(inout Material material)
{
	vec3 color = u_color;
	if(u_useDiffuseTex)
		color *= texture(u_diffuseTex, v_texCoord).rgb;
		
	material.diffuse = color;

	material.ambient = vec3(pow(1.0 - Fresnel(v_normal, 1.0), u_fresnelPow)) * u_edgeColor;
}