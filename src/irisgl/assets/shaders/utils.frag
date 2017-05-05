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