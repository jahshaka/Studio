/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

#version 150 core

uniform sampler2D tex;

in vec3 worldPosition;
in vec2 texCoord;

out vec4 fragColor;

uniform vec3 eyePosition;

uniform sampler2D diffuseTexture;

const int MAX_LIGHTS = 8;
const int TYPE_POINT = 0;
const int TYPE_DIRECTIONAL = 1;
const int TYPE_SPOT = 2;
struct Light {
    int type;
    vec3 position;
    vec3 color;
    float intensity;
    vec3 direction;
    vec3 attenuation;
    float cutOffAngle;
};
uniform Light lights[MAX_LIGHTS];
uniform int lightCount;

void adModel(const in vec3 vpos, const in vec3 vnormal, out vec3 diffuseColor)
{
    diffuseColor = vec3(0.0);

    vec3 n = normalize( vnormal );

    int i;
    vec3 s;
    for (i = 0; i < lightCount; ++i) {
        float att = 1.0;
        if ( lights[i].type != TYPE_DIRECTIONAL ) {
            s = lights[i].position - vpos;
            if (length( lights[i].attenuation ) != 0.0) {
                float dist = length(s);
                att = 1.0 / (lights[i].attenuation.x + lights[i].attenuation.y * dist + lights[i].attenuation.z * dist * dist);
            }
            s = normalize( s );
            if ( lights[i].type == TYPE_SPOT ) {
                if ( degrees(acos(dot(-s, normalize(lights[i].direction))) ) > lights[i].cutOffAngle)
                    att = 0.0;
            }
        } else {
            s = normalize( -lights[i].direction );
        }

        float diffuse = max( dot( s, n ), 0.0 );

        diffuseColor += att * lights[i].intensity * diffuse * lights[i].color;
    }
}

void main()
{
    vec3 diffuseTextureColor = texture( diffuseTexture, texCoord ).rgb;

    vec3 diffuseColor;
    adModel(worldPosition, vec3(0,1,0), diffuseColor);

    fragColor = vec4( diffuseTextureColor * diffuseColor, 1.0 );
    //fragColor = vec4( 1.0,1.0,1.0, 1.0 );
}
