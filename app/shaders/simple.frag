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

in vec2 v_texCoord;
in vec3 v_normal;
in vec3 v_worldPos;
in mat3 v_tanToWorld;

const int MAX_LIGHTS = 8;
const int TYPE_POINT = 0;
const int TYPE_DIRECTIONAL = 1;
const int TYPE_SPOT = 2;

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
    return vec2(x, y);
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
        vec3 texNorm = (texture2D(u_normalTexture,v_texCoord).xyz-0.5)*2;
        normal = normalize(v_tanToWorld*texNorm);
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


        diffuse += atten*ndl*u_lights[i].intensity*u_lights[i].color.rgb;
        specular += atten*spec* u_lights[i].intensity * u_lights[i].color.rgb;
    }

    vec3 col = u_material.diffuse;

    if(u_useDiffuseTex)
        col = col * texture2D(u_diffuseTexture,v_texCoord).rgb;

    if(u_useSpecularTex)
        specular = specular * texture2D(u_specularTexture,v_texCoord).rgb;


    vec3 finalColor = col*(u_material.ambient+diffuse)+
            (u_material.specular*specular);

    if(u_useReflectionTex)
    {
        vec3 incidentRay = normalize(v_worldPos-u_eyePos);
        vec3 reflVec = reflect(incidentRay,normal);
        vec3 reflCol = texture2D(u_reflectionTexture, envMapEquirect(normalize(reflVec))).rgb;

        finalColor = mix(finalColor,reflCol,u_reflectionInfluence);
    }

    if(u_fogData.enabled)
    {
        float zDist = length(v_worldPos-u_eyePos);
        float fogFactor = clamp((zDist-u_fogData.start)/(u_fogData.end-u_fogData.start),0,1);
        finalColor = mix(finalColor,u_fogData.color.rgb,fogFactor);
    }

    /*
    gl_FragColor = vec4(finalColor
                ,
                1.0);
                */
    fragColor = vec4(finalColor,1.0);
}
