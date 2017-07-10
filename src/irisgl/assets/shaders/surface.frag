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

in vec2 v_texCoord;
in vec3 v_normal;
in vec3 v_worldPos;
in mat3 v_tanToWorld;

const int MAX_LIGHTS = 8;
const int TYPE_POINT = 0;
const int TYPE_DIRECTIONAL = 1;
const int TYPE_SPOT = 2;

in vec4 FragPosLightSpace;
uniform sampler2D u_shadowMap;
uniform bool u_shadowEnabled;

float SampleShadowMap(sampler2D shadowMap, vec2 coords, float compare) {
    return step(compare, texture(shadowMap, coords.xy).r);
}

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

float SampleShadowMapPCF(sampler2D shadowMap, vec2 coords, float compare, vec2 texelSize) {
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

float CalcShadowMap(vec4 fragPosLightSpace) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    vec2 texelSize = 1.0 / textureSize(u_shadowMap, 0);
    return SampleShadowMapPCF(u_shadowMap, projCoords.xy, projCoords.z, texelSize);
}

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
};
uniform Light u_lights[MAX_LIGHTS];
uniform int u_lightCount;

uniform vec3 u_eyePos;
uniform vec3 u_sceneAmbient;

struct Material
{
    vec3 diffuse;
    vec3 specular;
    float shininess;
    vec3 normal;
    vec3 ambient;
    vec3 emission;
    float alpha;
};

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
    return vec2(x, y);
}

vec2 envMapEquirect(vec3 wcNormal) {
    //-1.0 for left handed coordinate system oriented texture (usual case)
    return envMapEquirect(wcNormal, -1.0);
}

vec3 NormalMapToWorldNormal(sampler2D tex, vec2 texCoord)
{
    vec3 texNorm = (texture(tex,texCoord).xyz-0.5)*2;
    return normalize(v_tanToWorld*texNorm);
}

// normal should be in world space
float Fresnel(vec3 normal, float power)
{
    vec3 I = normalize(u_eyePos-v_worldPos);
    return pow(dot(normal, I), power);
}

void surface(inout Material material);

void main()
{
    Material material;
    material.diffuse = vec3(0, 0, 0);
    material.specular = vec3(0, 0, 0);
    material.shininess = 0.0f;
    material.normal = v_normal;
    material.ambient = vec3(0, 0, 0);
    material.emission = vec3(0, 0, 0);
    material.alpha = 1.0f;
 
    surface(material);

    // ambient is scaled to the scene's ambient
    material.ambient *= u_sceneAmbient;

    vec3 diffuse = vec3(0);
    vec3 specular = vec3(0);    
    vec3 normal = material.normal;

    vec3 v = normalize(u_eyePos-v_worldPos);
    float spotCutoff = 1.0;

    for(int i=0;i<u_lightCount;i++)
    {
        float ndl = 0.0;
        vec3 n = normalize(normal);
        vec3 lightDir = u_lights[i].position-v_worldPos;//unnormalized
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

        if (ndl > 0.0 && material.shininess > 0.0) {
            float normFactor = (material.shininess + 2.0) / 2.0;//todo: find a better alternative
            vec3 r = reflect(-l, n);
            spec = normFactor*pow(max(dot(r, v), 0.0), material.shininess)*spotCutoff;
        }

        diffuse += atten*ndl*u_lights[i].intensity*u_lights[i].color.rgb;
        specular += atten*spec* u_lights[i].intensity * u_lights[i].color.rgb;
    }

    vec3 col = material.diffuse;

    float ShadowFactor = u_shadowEnabled ? CalcShadowMap(FragPosLightSpace) : 1.0;

    vec3 finalColor = (material.ambient + material.emission + (ShadowFactor *
                      (diffuse * col + (material.specular * specular))));


    if(u_fogData.enabled)
    {
        float zDist = length(v_worldPos-u_eyePos);
        float fogFactor = clamp((zDist-u_fogData.start)/(u_fogData.end-u_fogData.start),0,1);
        finalColor = mix(finalColor,u_fogData.color.rgb,fogFactor);
    }

    fragColor = vec4(finalColor, material.alpha);
}
