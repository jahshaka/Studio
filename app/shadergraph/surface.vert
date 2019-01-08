#version 150

in vec3 a_pos;
in vec2 a_texCoord;
in vec3 a_normal;
in vec3 a_tangent;

uniform mat4 matrix;
uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;
uniform mat3 u_normalMatrix;
uniform float u_textureScale;

uniform vec3 u_eyePos;
uniform vec3 u_sceneAmbient;
uniform float u_time = 0.0f;

out vec2 v_texCoord;
out vec3 v_localNormal;
out vec3 v_normal;
out vec3 v_worldPos;
out mat3 v_tanToWorld;//transforms from tangent space to world space

void surface(inout vec3 vertexOffset);

void main()
{
    v_localNormal = a_normal;
    v_texCoord = a_texCoord*u_textureScale;

    v_normal = normalize((u_normalMatrix*a_normal));
    //v_normal = normalize((u_worldMatrix*vec4(a_normal,0.0f)).xyz);
    vec3 v_tangent = normalize((u_normalMatrix*a_tangent));
    vec3 v_bitangent = cross(v_tangent,v_normal);

    v_tanToWorld = mat3(v_tangent,v_bitangent,v_normal);

    // calculated last because generated code might use vars above
    vec3 vertexOffset = vec3(0.0);
	surface(vertexOffset);

    v_worldPos = (u_worldMatrix*vec4(a_pos + vertexOffset,1.0)).xyz;
    gl_Position = u_projMatrix*u_viewMatrix*u_worldMatrix*vec4(a_pos + vertexOffset,1.0);
}
