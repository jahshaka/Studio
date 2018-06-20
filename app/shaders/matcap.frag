/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150

uniform sampler2D u_matTexture;
uniform bool u_useMatTexture;

in vec3 v_worldPos;
in vec3 v_viewNormal;
in vec3 v_viewEyeDir;

out vec4 fragColor;

//https://github.com/hughsk/matcap/blob/master/matcap.glsl
vec2 matcap(vec3 eye, vec3 normal) {
    vec3 reflected = reflect(eye, normal);
    float m = 2.8284271247461903 * sqrt( reflected.z+1.0 );
    return reflected.xy / m + 0.5;
}

void main() {
    //vec3 e = normalize(v_worldPos - u_eyePos);
    vec2 N = matcap(v_viewEyeDir, v_viewNormal);
    vec3 base = texture( u_matTexture, N ).rgb;

    if (u_useMatTexture) {
        fragColor = vec4(base, 1.0);
    } else {
        fragColor = vec4(1, 1, 0, 0.1);
    }
}
