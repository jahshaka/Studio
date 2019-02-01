#pragma include <surface.frag>

uniform sampler2D input_property0;
vec3 temp2;
vec4 temp1;
void surface(inout Material material){
// Texture Property - 189672d2-87b0-4061-b303-cdb198909845
temp1 = texture(input_property0,v_texCoord);
temp2 = temp1.xyz * vec3(2) - vec3(1);

// Surface Material - 7cf4d9d3-5d33-4168-9251-a92f73bf9e49
material.diffuse = temp1.xyz;
material.specular = vec3(0.0f, 0.0f, 0.0f);
material.shininess = 0.0f;
material.normal = vec3(0.0, 0.0, 1.0);
material.ambient = vec3(0.0f, 0.0f, 0.0f);
material.emission = vec3(0.0f, 0.0f, 0.0f);
material.alpha = 1.0f;
material.alphaCutoff = 0.0f;


}
