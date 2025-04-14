#pragma include <surface.frag>

uniform vec3 input_property0;
vec3 temp0;
void surface(inout Material material){
// Surface Material - 50aedc23-3ef8-44c8-b362-f09a4964e554
material.diffuse = input_property0;
material.specular = vec3(0.0f, 0.0f, 0.0f);
material.shininess = 0.0f;
material.normal = vec3(0.0, 0.0, 1.0);
material.ambient = vec3(0.0f, 0.0f, 0.0f);
material.emission = vec3(0.0f, 0.0f, 0.0f);
material.alpha = 1.0f;
material.alphaCutoff = 0.0f;


}
