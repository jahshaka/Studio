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

in vec3 worldPosition;
in vec2 texCoord;
in mat3 tangentMatrix;
in vec3 vertNormal;
//in vec3 reflVec;
//in vec3 incidentRay;

uniform sampler2D diffuseTexture;
uniform sampler2D reflTexture;
uniform sampler2D normalTexture;

// TODO: Replace with a struct
uniform vec3 ka;            // Ambient reflectivity
uniform vec3 ks;            // Specular reflectivity
uniform float shininess;    // Specular shininess factor

uniform vec3 eyePosition;
uniform float ior;//index of refraction

out vec4 fragColor;

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
    float constantAttenuation;
    float linearAttenuation;
    float quadraticAttenuation;
    float cutOffAngle;
};
uniform Light lights[MAX_LIGHTS];
uniform int lightCount;

void adsModel(const in vec3 vpos, const in vec3 vnormal, const in vec3 eye, const in float shininess,
              out vec3 diffuseColor, out vec3 specularColor)
{
    diffuseColor = vec3(0.0);
    specularColor = vec3(0.0);

    vec3 n = normalize( vnormal );

    int i;
    vec3 s;
    for (i = 0; i < lightCount; ++i) {
        float att = 1.0;
        if ( lights[i].type != TYPE_DIRECTIONAL ) {
            s = lights[i].position - vpos;
            if (lights[i].constantAttenuation != 0.0
             || lights[i].linearAttenuation != 0.0
             || lights[i].quadraticAttenuation != 0.0) {
                float dist = length(s);
                att = 1.0 / (lights[i].constantAttenuation + lights[i].linearAttenuation * dist + lights[i].quadraticAttenuation * dist * dist);
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

        float specular = 0.0;
        if (diffuse > 0.0 && shininess > 0.0 && att > 0.0) {
            vec3 r = reflect( -s, n );
            vec3 v = normalize( eye - vpos );
            float normFactor = ( shininess + 2.0 ) / 2.0;
            specular = normFactor * pow( max( dot( r, v ), 0.0 ), shininess );
        }

        diffuseColor += att * lights[i].intensity * diffuse * lights[i].color;
        specularColor += att * lights[i].intensity * specular * lights[i].color;
    }
}

vec2 envMapEquirect(vec3 wcNormal, float flipEnvMap) {
    float y = clamp( -1.0 * wcNormal.y * 0.5 + 0.5,0,1 );
    float x = atan(  wcNormal.z, wcNormal.x ) * RECIPROCAL_PI2 + 0.5;
    return vec2(x, y);
}

vec2 envMapEquirect(vec3 wcNormal) {
    //-1.0 for left handed coordinate system oriented texture (usual case)
    return envMapEquirect(wcNormal, -1.0);
}

void main()
{
    vec3 normal;
    // Sample the textures at the interpolated texCoords
    //vec3 normal = texture( normalTexture, texCoord ).rgb ;
    vec3 texNorm = texture( normalTexture, texCoord ).rgb;
    if(length(texNorm)<=0.000001)
        normal = vertNormal;
    else
    {
        normal = 2.0 * texNorm - vec3( 1.0 );
        //normal = normalize(tangentMatrix*normal);
        normal = tangentMatrix*normal;
    }

    //if(length(normal)<=0.000001)
    //    normal = vertNormal;

    // Calculate the lighting model, keeping the specular component separate
    //vec3 diffuseColor, specularColor;
    //adsModel(worldPosition, normal, eyePosition, shininess, diffuseColor, specularColor);

    vec3 incidentRay = normalize(worldPosition-eyePosition);
    //vec3 reflVec = reflect(incidentRay,vertNormal);
    //vec3 reflVec = refract(incidentRay,normal,1.0/ior);
    vec3 reflVec = refract(incidentRay,normal,1.0/1.333);
    //vec3 reflVec = refract(incidentRay,normal,0.000001);


    // Combine spec with ambient+diffuse for final fragment color
    //fragColor = vec4( ka + diffuseTextureColor.rgb * diffuseColor + ks * specularColor, 1.0 );
    //fragColor = texture( reflTexture, reflVec ).rgba;
    //fragColor = texture2D(reflTexture, envMapEquirect(normalize(reflVec)));
    fragColor = texture2D(reflTexture, envMapEquirect(normalize(reflVec)));
    //fragColor = vec4(normal,1.0);
    //fragColor = vec4(1,1,1,1);
}
