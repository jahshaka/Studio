#version 150
#define BLUR_HORIZONTAL 0
#define BLUR_VERTICAL 	1

in vec2 v_texCoord;

uniform sampler2D u_sceneTexture;
uniform int u_blurMode;

out vec4 fragColor;


uniform float weight[5] = float[] (0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);

void main()
{
	vec2 texOffset = 1.0 / textureSize(u_sceneTexture, 0);
	vec3 color = texture(u_sceneTexture, v_texCoord).rgb * weight[0];

	if (u_blurMode == BLUR_HORIZONTAL) {
		for(int i = 1; i < 5; i++) {
			color += texture(u_sceneTexture, v_texCoord + vec2(texOffset.x * i, 0.0)).rgb * weight[i];
            color += texture(u_sceneTexture, v_texCoord - vec2(texOffset.x * i, 0.0)).rgb * weight[i];
		}
 	} else {
 		for(int i = 1; i < 5; i++) {
			color += texture(u_sceneTexture, v_texCoord + vec2(0.0, texOffset.y * i)).rgb * weight[i];
            color += texture(u_sceneTexture, v_texCoord - vec2(0.0, texOffset.y * i)).rgb * weight[i];
		}
	}

	fragColor = vec4(color, 1.0);
}
