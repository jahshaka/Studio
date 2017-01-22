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

uniform sampler2D tex;

in vec3 worldPosition;
in vec2 texCoord;
in mat3 tangentMatrix;
in mat3 tanToWorld;
in vec3 vertNormal;

out vec4 fragColor;

uniform vec3 eyePosition;

uniform vec3 ambientCol;

uniform bool useDiffuseTex;
uniform sampler2D diffuseTexture;
uniform vec3 diffuseCol;

uniform bool useNormalTex;
uniform sampler2D normalTexture;
uniform float normalIntensity;

uniform bool useSpecularTex;
uniform sampler2D specularTexture;
uniform vec3 specularCol;
uniform float shininess;

uniform bool useReflectionTex;
uniform sampler2D reflectionTexture;
uniform float reflectionFactor;

uniform float textureScale;

//z-dist
//in float zDist;
uniform bool fogEnabled;
uniform float fogStart;
uniform float fogEnd;
uniform vec3 fogColor;

/* LIGHTS */
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

//used from qt3d's source
//https://github.com/qt/qt3d/blob/5.7/src/extras/shaders/gl3/light.inc.frag
void adsModel(const in vec3 worldPos,
              const in vec3 worldNormal,
              const in vec3 worldEye,
              const in float shininess,
              out vec3 diffuseColor,
              out vec3 specularColor)
{
    diffuseColor = vec3(0.0);
    specularColor = vec3(0.0);

    // We perform all work in world space
    vec3 n = normalize(worldNormal);
    vec3 v = normalize(worldEye - worldPos);
    vec3 s = vec3(0.0);

    for (int i = 0; i < lightCount; ++i) {
        float att = 1.0;
        float sDotN = 0.0;

        if (lights[i].type != TYPE_DIRECTIONAL) {
            // Point and Spot lights

            // Light position is already in world space
            vec3 sUnnormalized = lights[i].position - worldPos;
            s = normalize(sUnnormalized); // Light direction

            // Calculate the attenuation factor
            sDotN = dot(s, n);
            if (sDotN > 0.0) {
                if (lights[i].constantAttenuation != 0.0
                 || lights[i].linearAttenuation != 0.0
                 || lights[i].quadraticAttenuation != 0.0) {
                    float dist = length(sUnnormalized);
                    att = 1.0 / (lights[i].constantAttenuation +
                                 lights[i].linearAttenuation * dist +
                                 lights[i].quadraticAttenuation * dist * dist);
                }

                // The light direction is in world space already
                if (lights[i].type == TYPE_SPOT) {
                    // Check if fragment is inside or outside of the spot light cone
                    if (degrees(acos(dot(-s, lights[i].direction))) > lights[i].cutOffAngle)
                        sDotN = 0.0;
                }
            }
        } else {
            // Directional lights
            // The light direction is in world space already
            s = normalize(-lights[i].direction);
            sDotN = dot(s, n);
        }

        // Calculate the diffuse factor
        float diffuse = max(sDotN, 0.0);

        // Calculate the specular factor
        float specular = 0.0;
        if (diffuse > 0.0 && shininess > 0.0) {
            float normFactor = (shininess + 2.0) / 2.0;
            vec3 r = reflect(-s, n);   // Reflection direction in world space
            specular = normFactor * pow(max(dot(r, v), 0.0), shininess);
        }

        // Accumulate the diffuse and specular contributions
        diffuseColor += att * lights[i].intensity * diffuse * lights[i].color;
        specularColor += att * lights[i].intensity * specular * lights[i].color;
    }
}

//used from qt3d's source
//https://github.com/qt/qt3d/blob/5.7/src/extras/shaders/gl3/light.inc.frag
void adsModelNormalMapped(const in vec3 worldPos,
                          const in vec3 tsNormal,
                          const in vec3 worldEye,
                          const in float shininess,
                          const in mat3 tangentMatrix,
                          out vec3 diffuseColor,
                          out vec3 specularColor)
{
    diffuseColor = vec3(0.0);
    specularColor = vec3(0.0);

    // We perform all work in tangent space, so we convert quantities from world space
    vec3 tsPos = tangentMatrix * worldPos;
    vec3 n = normalize(tsNormal);
    vec3 v = normalize(tangentMatrix * (worldEye - worldPos));
    vec3 s = vec3(0.0);

    for (int i = 0; i < lightCount; ++i) {
        float att = 1.0;
        float sDotN = 0.0;

        if (lights[i].type != TYPE_DIRECTIONAL) {
            // Point and Spot lights

            // Transform the light position from world to tangent space
            vec3 tsLightPos = tangentMatrix * lights[i].position;
            vec3 sUnnormalized = tsLightPos - tsPos;
            s = normalize(sUnnormalized); // Light direction in tangent space

            // Calculate the attenuation factor
            sDotN = dot(s, n);
            if (sDotN > 0.0) {
                if (lights[i].constantAttenuation != 0.0
                 || lights[i].linearAttenuation != 0.0
                 || lights[i].quadraticAttenuation != 0.0) {
                    float dist = length(sUnnormalized);
                    att = 1.0 / (lights[i].constantAttenuation +
                                 lights[i].linearAttenuation * dist +
                                 lights[i].quadraticAttenuation * dist * dist);
                }

                // The light direction is in world space, convert to tangent space
                if (lights[i].type == TYPE_SPOT) {
                    // Check if fragment is inside or outside of the spot light cone
                    vec3 tsLightDirection = tangentMatrix * lights[i].direction;
                    if (degrees(acos(dot(-s, tsLightDirection))) > lights[i].cutOffAngle)
                        sDotN = 0.0;
                }
            }
        } else {
            // Directional lights
            // The light direction is in world space, convert to tangent space
            s = normalize(tangentMatrix * -lights[i].direction);
            sDotN = dot(s, n);
        }

        // Calculate the diffuse factor
        float diffuse = max(sDotN, 0.0);

        // Calculate the specular factor
        float specular = 0.0;
        if (diffuse > 0.0 && shininess > 0.0) {
            float normFactor = (shininess + 2.0) / 2.0;
            vec3 r = reflect(-s, n);   // Reflection direction in tangent space
            specular = normFactor * pow(max(dot(r, v), 0.0), shininess);
        }

        // Accumulate the diffuse and specular contributions
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
    vec3 col = diffuseCol;
    if(useDiffuseTex)
        col = texture( diffuseTexture, texCoord * textureScale ).rgb * col;

    vec3 diffuseLighting;
    vec3 specularLighting;
    vec3 worldNormal = vertNormal;

    if(useNormalTex)
    {
        vec3 normal = texture( normalTexture, texCoord * textureScale ).rgb;
        normal = 2.0 * normal - vec3( 1.0 );
        //normal.z = normal.z*normalIntensity;
        //normal = normalize(normal);
        normal = normalize(mix(vec3(0,0,1),normal,normalIntensity));

        adsModelNormalMapped(worldPosition,
                             normal,
                             eyePosition,
                             shininess,
                             tangentMatrix,
                             diffuseLighting,
                             specularLighting
                             );

        //convert from tangent space to world space
        worldNormal = tanToWorld*normal;
    }
    else
    {
        adsModel(worldPosition,vertNormal,eyePosition,shininess,diffuseLighting,specularLighting);
    }

    vec3 spec = specularCol;
    if(useSpecularTex)
        spec = texture( specularTexture, texCoord * textureScale ).rgb * spec;

    vec3 finalCol = col * (ambientCol+diffuseLighting) + (spec*specularLighting);

    if(useReflectionTex)
    {
        vec3 incidentRay = normalize(worldPosition-eyePosition);
        vec3 reflVec = reflect(incidentRay,worldNormal);
        vec3 reflCol = texture2D(reflectionTexture, envMapEquirect(normalize(reflVec))).rgb;

        finalCol = mix(finalCol,reflCol,reflectionFactor);
    }

    //fog
    if(fogEnabled)
    {
        float zDist = length(worldPosition-eyePosition);
        float fogFactor = clamp((zDist-fogStart)/(fogEnd-fogStart),0,1);
        finalCol = mix(finalCol,fogColor,fogFactor);
    }


    fragColor = vec4(finalCol , 1.0 );
    //fragColor = vec4(fogColor , 1.0 );
    //fragColor = vec4( 1.0,1.0,1.0, 1.0 );
    //fragColor = vec4(ambientCol,1.0);
}
