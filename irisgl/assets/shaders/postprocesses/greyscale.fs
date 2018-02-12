#version 150

in vec2 v_texCoord;

uniform sampler2D u_sceneTexture;
uniform float threshold;

out vec4 fragColor;


void main()
{
	vec4 color = texture(u_sceneTexture, v_texCoord );
	float brightness = dot(color.rgb, vec3(0.2126, 0.7152, 0.0722));
	
	fragColor = vec4(vec3(brightness), 1);
}
