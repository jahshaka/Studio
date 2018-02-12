#version 150

in vec3 a_pos;
in vec2 a_texCoord;

//out vec3 v_pos;
out vec2 v_texCoord;

uniform mat4 matrix;

void main()
{
	gl_Position = vec4(a_pos, 1.0);
	v_texCoord = a_texCoord;
}