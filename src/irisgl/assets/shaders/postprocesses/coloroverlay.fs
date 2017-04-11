#version 150

in vec2 v_texCoord;

uniform sampler2D u_sceneTexture;
uniform vec3 u_colorOverlay;

out vec4 fragColor;

void main()
{
	vec4 color = texture(u_sceneTexture, v_texCoord);
        //fragColor = vec4(color.rgb * u_colorOverlay, color.a);
        fragColor = vec4(color.rgb * vec3(1.0,0.1f,0.1f), color.a);
}
