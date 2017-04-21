#version 150

in vec2 v_texCoord;

uniform sampler2D u_sceneTexture;
uniform sampler2D u_hBlurTexture;
uniform sampler2D u_vBlurTexture;

uniform sampler2D u_dirtTexture;
uniform bool u_useDirt;
uniform float u_dirtStrength;

uniform float u_bloomStrength;

out vec4 fragColor;


void main()
{
	vec4 color = texture(u_sceneTexture, v_texCoord);
	vec4 blur = texture(u_hBlurTexture, v_texCoord);
	blur += texture(u_vBlurTexture, v_texCoord);

        vec4 bloom = blur * vec4(u_bloomStrength);
        color += bloom;

        if (u_useDirt) {
            color += texture(u_dirtTexture, v_texCoord) * blur * u_dirtStrength;
        }

        fragColor = color;
}
