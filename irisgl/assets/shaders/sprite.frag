#version 150

in vec4 v_color;
in vec2 v_texCoord;

uniform sampler2D tex;

out vec4 fragColor;
void main()
{
        vec4 finalCol = texture(tex, v_texCoord) * v_color;
        fragColor = finalCol;
}
