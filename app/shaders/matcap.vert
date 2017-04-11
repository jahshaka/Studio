#version 150

in vec3 a_pos;
in vec2 a_texCoord;
in vec3 a_normal;

uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;
uniform mat3 u_normalMatrix;
uniform float u_textureScale;

out vec2 v_texCoord;
out vec3 v_normal;
out vec3 v_worldPos;

void main() {
    v_worldPos = (u_viewMatrix * u_worldMatrix * vec4(a_pos, 1.0)).xyz;
    gl_Position = u_projMatrix * u_viewMatrix * u_worldMatrix * vec4(a_pos, 1.0);
    v_texCoord = a_texCoord * u_textureScale;
    v_normal = u_normalMatrix * a_normal;
}
