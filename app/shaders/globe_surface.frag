#pragma include <surface.frag>

uniform vec3 u_color;
uniform bool u_useDiffuseTex;
uniform sampler2D u_diffuseTex;
uniform sampler2D u_specularTex;
uniform sampler2D u_nightTex;
uniform sampler2D u_normalTex;

uniform float u_fresnelPow;
uniform vec3 u_edgeColor;

void surface(inout Material material)
{
	vec3 color = u_color;
	if(u_useDiffuseTex)
		color *= texture(u_diffuseTex, v_texCoord).rgb;
		
	material.diffuse = color;
    material.specular = (1.0 - texture(u_specularTex, v_texCoord).bbb) * 0.05;
	material.shininess = 30;

	material.normal = v_tanToWorld * ((texture(u_normalTex, v_texCoord).rgb * vec3(2)) - vec3(1.0));


	material.diffuse += vec3(pow(1.0 - Fresnel(v_normal, 1.0), u_fresnelPow)) * u_edgeColor;
	
	//material.diffuse = vec3(0,0,0);

	// only do this for the primary light
	//float light = max(0.0,dot(normalize(u_lights[1].position - v_worldPos),normalize(-v_normal)));

	// for dir light
	float light = max(0.0,dot(normalize(-u_lights[0].direction),normalize(-v_normal)));

	material.ambient += texture(u_nightTex, v_texCoord).rgb * light * 2;
	
	//material.diffuse = texture(u_nightTex, v_texCoord).rgb * light * 2;
	//material.ambient += vec3(light, light, light);
}
