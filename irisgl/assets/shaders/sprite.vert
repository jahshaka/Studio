#version 150

in vec3 a_pos;
in vec4 a_color;
in vec2 a_texCoord;

out vec4 v_color;
out vec2 v_texCoord;

uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;

void main()
{
        gl_Position = u_projMatrix * u_viewMatrix * vec4(a_pos, 1.0);

	v_color = a_color;
	v_texCoord = a_texCoord;
}
