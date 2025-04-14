#version 150

in vec3 a_pos;
in vec2 a_texCoord;
in vec3 a_normal;
in vec3 a_tangent;

#ifdef SKINNING_ENABLED
const int MAX_BONES = 100;
uniform mat4 u_bones[MAX_BONES];
in vec4 a_boneWeights;
in vec4 a_boneIndices;
#endif

uniform mat4 matrix;
uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;
uniform mat3 u_normalMatrix;
uniform float u_textureScale = 1.0;

uniform vec3 u_eyePos;
uniform vec3 u_sceneAmbient;
uniform float u_time = 0.0f;

out vec2 v_texCoord;
out vec3 v_localNormal;
out vec3 v_normal; // world normal
out vec3 v_worldPos;
out mat3 v_tanToWorld;//transforms from tangent space to world space

void surface(inout vec3 vertexOffset, inout float vertexExtrusion);

void main()
{
#ifdef SKINNING_ENABLED
    mat4 boneMatrix = u_bones[int(a_boneIndices[0])] * a_boneWeights[0];
    boneMatrix += u_bones[int(a_boneIndices[1])] * a_boneWeights[1];
    boneMatrix += u_bones[int(a_boneIndices[2])] * a_boneWeights[2];
    boneMatrix += u_bones[int(a_boneIndices[3])] * a_boneWeights[3];    

    v_texCoord = a_texCoord*u_textureScale;

    v_localNormal = a_normal;

    vec3 skinnedNormal = (boneMatrix * vec4(a_normal,0)).xyz;
    v_normal = normalize((u_normalMatrix*skinnedNormal));

    vec3 skinnedTangent = (boneMatrix * vec4(a_tangent,0)).xyz;
    vec3 v_tangent = normalize((u_normalMatrix*skinnedTangent));

    vec3 v_bitangent = cross(v_tangent,v_normal);

    v_tanToWorld = mat3(v_tangent,v_bitangent,v_normal);

    // calculated last because generated code might use vars above
    v_localNormal = skinnedNormal;
    vec3 vertexOffset = vec3(0.0);
    float vertexExtrusion = 0.0;
    surface(vertexOffset, vertexExtrusion);

    v_worldPos = (u_worldMatrix*boneMatrix*vec4(a_pos + vertexOffset + vertexExtrusion * v_localNormal,1.0)).xyz;
    gl_Position = u_projMatrix*u_viewMatrix*u_worldMatrix*boneMatrix*vec4(a_pos + vertexOffset + vertexExtrusion * v_localNormal,1.0);

#else

    v_localNormal = a_normal;
    v_texCoord = a_texCoord*u_textureScale;

    v_normal = normalize((u_normalMatrix*a_normal));
    vec3 v_tangent = normalize((u_normalMatrix*a_tangent));
    vec3 v_bitangent = cross(v_tangent,v_normal);

    v_tanToWorld = mat3(v_tangent,v_bitangent,v_normal);

    // calculated last because generated code might use vars above
    vec3 vertexOffset = vec3(0.0);
	float vertexExtrusion = 0.0;
	surface(vertexOffset, vertexExtrusion);

    v_worldPos = (u_worldMatrix*vec4(a_pos + vertexOffset + vertexExtrusion * v_localNormal,1.0)).xyz;
    gl_Position = u_projMatrix*u_viewMatrix*u_worldMatrix*vec4(a_pos + vertexOffset + vertexExtrusion * v_localNormal,1.0);

#endif
}
