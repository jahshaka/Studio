#pragma include <surface.frag>

uniform sampler2D input_property0;
uniform sampler2D input_property1;
uniform float input_property3;
uniform sampler2D input_property2;
float temp6;
vec4 temp4;
vec4 temp8;
vec3 temp5;
vec3 temp9;
vec3 temp2;
vec4 temp1;
void surface(inout Material material){
// Diffuse - d36eae76-9268-4943-9fde-9ecfa2aa0a59
temp8 = texture(input_property0,v_texCoord);
temp9 = temp8.xyz * vec3(2) - vec3(1);

// Specular - 3ee7a476-2476-4850-b3fa-55698ba4ab5d
temp1 = texture(input_property1,v_texCoord);
temp2 = temp1.xyz * vec3(2) - vec3(1);

// normal - bf009387-a076-40e6-ae94-af38afb78fef
temp4 = texture(input_property2,v_texCoord);
temp5 = temp4.xyz * vec3(2) - vec3(1);

// Surface Material - fc0e39bd-23b5-4946-8d46-89505509486d
material.diffuse = temp8.xyz;
material.specular = temp1.xyz;
material.shininess = input_property3;
material.normal = temp5;
material.ambient = vec3(0.0f, 0.0f, 0.0f);
material.emission = vec3(0.0f, 0.0f, 0.0f);
material.alpha = 1.0f;
material.alphaCutoff = 0.0f;


}
