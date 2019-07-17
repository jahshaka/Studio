#pragma include <surface.frag>

uniform sampler2D input_property0;
uniform sampler2D input_property1;
uniform float input_property3;
uniform sampler2D input_property2;
<<<<<<< HEAD
float temp9;
vec4 temp7;
vec3 temp2;
vec3 temp8;
vec3 temp5;
vec4 temp1;
vec4 temp4;
void surface(inout Material material){
// Texture Property - b82e0470-5cc5-402f-929f-7e46525a26c1
temp7 = texture(input_property0,v_texCoord);
temp8 = temp7.xyz * vec3(2) - vec3(1);

// Texture Property - 1df3b187-3e0d-42c9-a174-4da9b5f066d0
temp1 = texture(input_property1,v_texCoord);
temp2 = temp1.xyz * vec3(2) - vec3(1);

// Texture Property - 45434c79-69db-4e2c-9b31-7014bbea85c1
temp4 = texture(input_property2,v_texCoord);
temp5 = temp4.xyz * vec3(2) - vec3(1);

// Surface Material - 5967cd47-5659-4ac2-b59d-1acdc9443cd6
material.diffuse = temp7.xyz;
material.specular = temp1.xyz;
material.shininess = input_property3;
material.normal = temp4.xyz;
=======
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
>>>>>>> 8588c861a4bbdf3f73a1786bf0c614db2715f5df
material.ambient = vec3(0.0f, 0.0f, 0.0f);
material.emission = vec3(0.0f, 0.0f, 0.0f);
material.alpha = 1.0f;
material.alphaCutoff = 0.0f;


}
