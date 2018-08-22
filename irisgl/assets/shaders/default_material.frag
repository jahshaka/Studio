/**************************************************************************
This file is part of JahshakaVR, VR Authoring Toolkit
http://www.jahshaka.com
Copyright (c) 2016  GPLv3 Jahshaka LLC <coders@jahshaka.com>

This is free software: you may copy, redistribute
and/or modify it under the terms of the GPLv3 License

For more information see the LICENSE file
*************************************************************************/

//http://www.tomdalling.com/blog/modern-opengl/07-more-lighting-ambient-specular-attenuation-gamma/

#version 150

#define PI 3.14159265359
#define PI2 6.28318530718
#define RECIPROCAL_PI2 0.15915494

#define SHADOW_NONE 0
#define SHADOW_HARD 1
#define SHADOW_SOFT 2
#define SHADOW_VERYSOFT 3

uniform sampler2D u_diffuseTexture;
uniform bool u_useDiffuseTex;

uniform sampler2D u_normalTexture;
uniform bool u_useNormalTex;
uniform float u_normalIntensity;

uniform sampler2D u_specularTexture;
uniform bool u_useSpecularTex;

uniform sampler2D u_reflectionTexture;
uniform float u_reflectionInfluence;
uniform bool u_useReflectionTex;

uniform bool u_useAlpha;

in vec2 v_texCoord;
in vec3 v_normal;
in vec3 v_worldPos;
in mat3 v_tanToWorld;

const int MAX_LIGHTS = 8;
const int TYPE_POINT = 0;
const int TYPE_DIRECTIONAL = 1;
const int TYPE_SPOT = 2;

//const int SHADOW_NONE = 0;
//const int SHADOW_HARD = 1;
//const int SHADOW_SOFT = 2;
//const int SHADOW_SOFTER = 3;

//in vec4 FragPosLightSpace;
//uniform sampler2D u_shadowMap;
//uniform bool u_shadowEnabled;
struct Light {
    int type;
    vec3 position;
    vec3 ambient;
    vec4 color;
    float distance;
    float intensity;
    vec3 direction;
    float cutOffAngle;
    float cutOffSoftness;

	vec4 shadowColor;
	float shadowAlpha;

    sampler2D shadowMap;
    bool shadowEnabled;
    int shadowType;
    mat4 shadowMatrix;
};

float SampleShadowMap(in sampler2D shadowMap, vec2 coords, float compare) {
    if (coords.x < 0.0 || coords.x > 1.0 || coords.y < 0.0 || coords.y > 1.0)
        return 1.0;
    return step(compare, texture(shadowMap, coords.xy).r);
}

// todo: use sampler2DShadow, it does the same thing but faster
float SampleShadowMapLinear(sampler2D shadowMap, vec2 coords, float compare, vec2 texelSize) {
    vec2 pixelPos = coords / texelSize + vec2(0.5);
    vec2 fracPart = fract(pixelPos);
    vec2 startTexel = (pixelPos - fracPart) * texelSize;

    float blTexel = SampleShadowMap(shadowMap, startTexel, compare);
    float brTexel = SampleShadowMap(shadowMap, startTexel + vec2(texelSize.x, 0.0), compare);
    float tlTexel = SampleShadowMap(shadowMap, startTexel + vec2(0.0, texelSize.y), compare);
    float trTexel = SampleShadowMap(shadowMap, startTexel + texelSize, compare);

    float mixA = mix(blTexel, tlTexel, fracPart.y);
    float mixB = mix(brTexel, trTexel, fracPart.y);

    return mix(mixA, mixB, fracPart.x);
}

float SampleShadowMapPCF(in sampler2D shadowMap, vec2 coords, float compare, vec2 texelSize) {
    float result = 0;

    const float NUM_SAMPLES = 7.0;
    const float SAMPLES_START = (NUM_SAMPLES - 1.0) / 2.0;

    for(float y = -SAMPLES_START; y <= SAMPLES_START; y++) {
        for(float x = -SAMPLES_START; x <= SAMPLES_START; x++) {
            vec2 offset = vec2(x, y) * texelSize;
            result += SampleShadowMapLinear(shadowMap, coords + offset, compare, texelSize);
        }
    }

    return result / (NUM_SAMPLES * NUM_SAMPLES);
}

// it's technically 4x4...but it will do for now
float SampleShadowMapPCF3x3(in sampler2D shadowMap, vec2 coords, float compare, vec2 texelSize) {
    float result = 0;

    const float NUM_SAMPLES = 3.0;
    const float SAMPLES_START = (NUM_SAMPLES - 1.0) / 2.0;

    for(float y = -SAMPLES_START; y <= SAMPLES_START; y++) {
        for(float x = -SAMPLES_START; x <= SAMPLES_START; x++) {
            vec2 offset = vec2(x, y) * texelSize;
            result += SampleShadowMapLinear(shadowMap, coords + offset, compare, texelSize);
        }
    }

    return result / (NUM_SAMPLES * NUM_SAMPLES);
}


float CalcShadowMap(in sampler2D shadowMap, vec4 fragPosLightSpace) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    return SampleShadowMapPCF(shadowMap, projCoords.xy, projCoords.z, texelSize);
}

float calcVerySoftShadowMap(in Light light, in vec4 lightSpacePos)
{
    return CalcShadowMap(light.shadowMap, lightSpacePos);
}

float calcSoftShadowMap(in Light light, in vec4 fragPosLightSpace)
{
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    vec2 texelSize = 1.0 / textureSize(light.shadowMap, 0);
    return SampleShadowMapPCF3x3(light.shadowMap, projCoords.xy, projCoords.z, texelSize);
}

float calcHardShadowMap(in Light light, in vec4 lightSpacePos)
{
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords = projCoords * 0.5 + 0.5;
    //return SampleShadowMap(light.shadowMap,projCoords.xy,projCoords.z);
    if (projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 1.0;
    if (projCoords.z > texture(light.shadowMap, projCoords.xy).r)
        return 0.0;
    return 1.0;
}


//  Handles shadowing for lights with different shadowing types
float calculateShadowFactor(in Light light, in vec3 worldPos)
{
    vec4 lightSpacePos = light.shadowMatrix * vec4(v_worldPos, 1.0);
    if (light.shadowType==SHADOW_HARD)
        return calcHardShadowMap(light, lightSpacePos);
    if (light.shadowType==SHADOW_SOFT)
        return calcSoftShadowMap(light, lightSpacePos);
	if (light.shadowType==SHADOW_VERYSOFT)
        return calcVerySoftShadowMap(light, lightSpacePos);
    return 1.0f;
}



uniform sampler2D shadowMaps[MAX_LIGHTS];

uniform Light u_lights[MAX_LIGHTS];
uniform int u_lightCount;

uniform vec3 u_eyePos;
uniform vec3 u_sceneAmbient; 

struct Material
{
    vec3 diffuse;
    vec3 specular;
    float shininess;
    vec3 ambient;
};

//uniform vec3 mat_diffuse;

uniform Material u_material;

struct Fog
{
    bool enabled;
    float start;
    float end;
    vec4 color;
};

uniform Fog u_fogData;

out vec4 fragColor;

vec2 envMapEquirect(vec3 wcNormal, float flipEnvMap) {
    float y = clamp( -1.0 * wcNormal.y * 0.5 + 0.5,0,1 );
    float x = atan(  wcNormal.z, wcNormal.x ) * RECIPROCAL_PI2 + 0.5;
    return vec2(x, y * flipEnvMap);
}

vec2 envMapEquirect(vec3 wcNormal) {
    //-1.0 for left handed coordinate system oriented texture (usual case)
    return envMapEquirect(wcNormal, -1.0);
}

void main()
{
    vec3 diffuse = vec3(0);
    vec3 specular = vec3(0);

    vec3 normal = v_normal;

    //normal mapping
    if(u_useNormalTex)
    {
        vec3 texNorm = (texture(u_normalTexture,v_texCoord).xyz-0.5)*2;
		texNorm.xy *= u_normalIntensity;
        normal = normalize(v_tanToWorld*texNorm);
		//normal = normalize(normal);
        //normal = mix(normal,vec3(0,0,0),u_normalIntensity);
    }

    vec3 v = normalize(u_eyePos-v_worldPos);
    float spotCutoff = 1.0;

    for(int i=0;i<u_lightCount;i++)
    {
        float ndl = 0.0;
        vec3 n = normalize(normal);
        vec3 lightDir = u_lights[i].position-v_worldPos;//unnormlaized
        vec3 l = normalize(lightDir);
        float atten = 1.0;

        if(u_lights[i].type!=TYPE_DIRECTIONAL)//point and spot
        {
            ndl = max(dot(n,l),0.0);

            if(ndl>0)
            {
                float lightDist = length(lightDir);

                //atten = 1.0-smoothstep(0.0,u_lights[i].distance,lightDist);
                atten = clamp((lightDist-u_lights[i].distance)/(-u_lights[i].distance),0.0,1.0);
                atten = atten*atten;

                //attenuation
                //https://developer.valvesoftware.com/wiki/Constant-Linear-Quadratic_Falloff
                //http://brabl.com/light-attenuation/
                //http://gamedev.stackexchange.com/questions/56897/glsl-light-attenuation-color-and-intensity-formula

            }

            if(u_lights[i].type==TYPE_SPOT)
            {
                float cos_angle = degrees(acos(dot(-l, u_lights[i].direction)));
                spotCutoff = clamp((cos_angle-u_lights[i].cutOffAngle)/
                                   (-u_lights[i].cutOffSoftness)
                                  ,
                                  0.0,1.0);
                ndl = ndl*spotCutoff;
            }


        }
        else
        {
            //directional
            l = normalize(-u_lights[i].direction);
            ndl = max(dot(l, n),0.0);
        }

        float spec = 0.0;

        if (ndl > 0.0 && u_material.shininess > 0.0) {
            float normFactor = (u_material.shininess + 2.0) / 2.0;//todo: find a better alternative
            vec3 r = reflect(-l, n);
            spec = normFactor*pow(max(dot(r, v), 0.0), u_material.shininess)*spotCutoff;
            //spec = pow(max(dot(r, v), 0.0), 0.7f);
        }

        //vec4 FragPosLightSpace = u_lights[i].shadowMatrix * vec4(v_worldPos, 1.0);
        //float shadowFactor = u_lights[i].shadowEnabled ? CalcShadowMap(u_lights[i].shadowMap,FragPosLightSpace) : 1.0;
        float shadowFactor = calculateShadowFactor(u_lights[i], v_worldPos);

		float shadow = mix(1.0, shadowFactor, u_lights[i].shadowAlpha);
        diffuse += mix(u_lights[i].shadowColor.rgb, atten*ndl*u_lights[i].intensity*u_lights[i].color.rgb, shadow);
        specular += mix(u_lights[i].shadowColor.rgb, atten*spec* u_lights[i].intensity * u_lights[i].color.rgb, shadow);

		//diffuse += atten*ndl*u_lights[i].intensity*u_lights[i].color.rgb*shadowFactor;
        //specular += atten*spec* u_lights[i].intensity * u_lights[i].color.rgb*shadowFactor;
    }

    vec3 col = u_material.diffuse;

    if (u_useDiffuseTex) {
        col = col * texture(u_diffuseTexture, v_texCoord).rgb;
        if (u_useAlpha && texture(u_diffuseTexture, v_texCoord).a < 0.5) discard;
    }

    if(u_useSpecularTex)
        specular = specular * texture(u_specularTexture,v_texCoord).rgb;

    /*
    float ShadowFactor = u_shadowEnabled ? CalcShadowMap(FragPosLightSpace) : 1.0;

    vec3 finalColor = ((u_material.ambient * u_sceneAmbient) + (ShadowFactor *
                      (diffuse + (u_material.specular * specular)))) * col;
    */
    vec3 finalColor = ((u_material.ambient * u_sceneAmbient) + (
                      (diffuse + (u_material.specular * specular)))) * col;

    if(u_useReflectionTex)
    {
        vec3 incidentRay = normalize(v_worldPos-u_eyePos);
        vec3 reflVec = reflect(incidentRay,normal);
        vec3 reflCol = texture(u_reflectionTexture, envMapEquirect(normalize(reflVec))).rgb;

        finalColor = mix(finalColor,reflCol,u_reflectionInfluence);
    }

    if(u_fogData.enabled)
    {
        float zDist = length(v_worldPos-u_eyePos);
        float fogFactor = clamp((zDist-u_fogData.start)/(u_fogData.end-u_fogData.start),0,1);
        finalColor = mix(finalColor,u_fogData.color.rgb,fogFactor);
    }

    fragColor = vec4(finalColor, 0.65);
}
