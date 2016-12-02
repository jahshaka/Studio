//#version 330
//http://www.tomdalling.com/blog/modern-opengl/07-more-lighting-ambient-specular-attenuation-gamma/

#version 150
#define PI 3.14159265359
#define PI2 6.28318530718
#define RECIPROCAL_PI2 0.15915494

uniform sampler2D u_diffuseTexture;
uniform bool u_useDiffuseTex;

uniform sampler2D u_normalTexture;
uniform bool u_useNormalTex;

uniform sampler2D u_specularTexture;
uniform bool u_useSpecularTex;

uniform sampler2D u_reflectionTexture;
uniform float u_reflectionInfluence;
uniform bool u_useReflectionTex;

varying vec2 v_texCoord;
varying vec3 v_normal;
varying vec3 v_worldPos;
varying mat3 v_tanToWorld;

const int MAX_LIGHTS = 8;
const int TYPE_POINT = 0;
const int TYPE_DIRECTIONAL = 1;
const int TYPE_SPOT = 2;

struct Light {
    int type;
    vec3 position;
    vec3 ambient;
    vec4 color;
    float range;
    float intensity;
    vec3 direction;
    float constantAtten;
    float linearAtten;
    float quadraticAtten;
    float cutOffAngle;
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
    }

    vec3 v = normalize(u_eyePos-v_worldPos);

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
                atten = 1.0/(u_lights[i].constantAtten +
                        u_lights[i].linearAtten * lightDist +
                        u_lights[i].quadraticAtten * lightDist * lightDist);

                //attenuation
                //https://developer.valvesoftware.com/wiki/Constant-Linear-Quadratic_Falloff
                //http://brabl.com/light-attenuation/
                //http://gamedev.stackexchange.com/questions/56897/glsl-light-attenuation-color-and-intensity-formula

            }

            if(u_lights[i].type==TYPE_SPOT)
            {
                //todo: soft edges for spot cut-off
                if (degrees(acos(dot(-l, u_lights[i].direction))) > u_lights[i].cutOffAngle)
                //if (degrees(acos(dot(-l, u_lights[i].direction))) > 30)
                    ndl = 0.0;
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
            spec = normFactor*pow(max(dot(r, v), 0.0), u_material.shininess);
            //spec = pow(max(dot(r, v), 0.0), 0.7f);
        }


        //diffuse += atten*ndl*u_lights[i].intensity*u_lights[i].color.rgb;
        //diffuse += u_lights[i].intensity*u_lights[i].color.rgb;
        diffuse += atten*ndl*u_lights[i].intensity*u_lights[i].color.rgb;
        specular += atten*spec* u_lights[i].intensity * u_lights[i].color.rgb;
        //specular += spec;
    }

    //diffuse = vec3(1,1,1);

    //vec3 col = vec3(1,0,1);
    vec3 col = u_material.diffuse;
    //vec3 col = mat_diffuse;
    if(u_useDiffuseTex)
        col = col * texture2D(u_diffuseTexture,v_texCoord).rgb;

    if(u_useSpecularTex)
        specular = specular * texture2D(u_specularTexture,v_texCoord).rgb;


    vec3 finalColor = col*(u_material.ambient+diffuse)+
            (u_material.specular*specular);

    //vec3 finalColor = col;

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

    gl_FragColor = vec4(finalColor
                ,
                1.0);

    /*
    gl_FragColor = vec4(
                (u_material.specular*specular),
                1.0);
    */
    //gl_FragColor = vec4(diffuse,1.0);
    //gl_FragColor = vec4(v_normal,1.0);
}
