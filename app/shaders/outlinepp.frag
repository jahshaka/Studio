/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150

in vec3 v_pos;
in vec2 v_texCoord;

out vec4 fragColor;

uniform sampler2D u_sceneTex;
uniform float u_lineWidth;
uniform vec3 u_color;

// https://github.com/spite/Wagner/blob/master/fragment-shaders/sobel-fs.glsl
// https://www.shadertoy.com/view/Xdf3Rf

vec4 sampleScreen(sampler2D tex, vec2 coords)
{
	return vec4(texture(tex, coords).a);
}

void buildKernel(inout vec4 n[9], sampler2D tex, vec2 coord)
{
	// https://stackoverflow.com/questions/25803909/glsl-texture-size
	vec2 texSize = vec2(1.0, 1.0) / textureSize(u_sceneTex, 0);
	float w = texSize.x * u_lineWidth;
	float h = texSize.y * u_lineWidth;

	n[0] = sampleScreen(tex, coord + vec2( -w, -h));
	n[1] = sampleScreen(tex, coord + vec2(0.0, -h));
	n[2] = sampleScreen(tex, coord + vec2(  w, -h));
	n[3] = sampleScreen(tex, coord + vec2( -w, 0.0));
	n[4] = sampleScreen(tex, coord);
	n[5] = sampleScreen(tex, coord + vec2(  w, 0.0));
	n[6] = sampleScreen(tex, coord + vec2( -w, h));
	n[7] = sampleScreen(tex, coord + vec2(0.0, h));
	n[8] = sampleScreen(tex, coord + vec2(  w, h));
}

void main()
{
	vec4 n[9];
	buildKernel( n, u_sceneTex, v_texCoord);

	vec4 h = n[2] + (2.0*n[5]) + n[8] - (n[0] + (2.0*n[3]) + n[6]);
  	vec4 v = n[0] + (2.0*n[1]) + n[2] - (n[6] + (2.0*n[7]) + n[8]);

	fragColor = min(sqrt((h * h) + (v * v)), 1.0) * vec4(u_color, 1.0);
}