#version 150

in vec2 v_texCoord;

uniform sampler2D u_sceneTexture;

out vec4 fragColor;


// https://www.shadertoy.com/view/XsfSDs
const int samples = 10;

void main()
{
	vec4 color = vec4(0.0);

	float blurStart = 1.0f;
	float blurWidth = 0.1f;// in uv coords

	float increment = blurWidth * (1.0f / (samples-1));
	vec2 center = vec2(0.5, 0.5);
	vec2 dir = v_texCoord - center;

	for (int i = 0; i < samples; i++) {
		float scale = blurStart + i * increment;
		color += texture(u_sceneTexture, center + dir * scale );
	}

	color /= float(samples);
	fragColor = color;
}
