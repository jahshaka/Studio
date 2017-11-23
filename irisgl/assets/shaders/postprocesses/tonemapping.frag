#version 150

uniform float exposure = 0.0;
uniform float gamma = 2.2;

vec3 reinhard(vec3 hdrCol)
{
    return hdrCol/ (hdrCol + vec3(1.0));
}

vec3 gammaCorrect(vec3 linear)
{
    return pow(linear, vec3(1.0/gamma));
}

in vec2 v_texCoord;
uniform sampler2D u_screenTex;

out vec4 fragColor;
void main()
{
	vec3 color = texture(u_screenTex, v_texCoord).rgb;

	// convert to LDR here...

	vec3 gammaCol = gammaCorrect(color);

	fragColor = vec4(gammaCol,
		           dot(gammaCol, vec3(0.299, 0.587, 0.114))); // for fxaa
}