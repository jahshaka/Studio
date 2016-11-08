//#version 330
#version 150

attribute vec3 a_pos;
attribute vec2 a_texCoord;
attribute vec3 a_normal;
attribute vec3 a_tangent;

uniform mat4 matrix;
uniform mat4 u_viewMatrix;
uniform mat4 u_projMatrix;
uniform mat4 u_worldMatrix;
uniform mat3 u_normalMatrix;
uniform float u_textureScale;

varying vec2 v_texCoord;
varying vec3 v_normal;
varying vec3 v_worldPos;
varying mat3 v_tanToWorld;//transforms from tangent space to world space

void main()
{
    v_worldPos = (u_worldMatrix*vec4(a_pos,1.0)).xyz;
    //gl_Position = matrix*vec4(a_pos,1.0);
    gl_Position = u_projMatrix*u_viewMatrix*u_worldMatrix*vec4(a_pos,1.0);

    v_texCoord = a_texCoord*u_textureScale;
    //v_texCoord = a_texCoord*2;

    v_normal = normalize((u_normalMatrix*a_normal));
    vec3 v_tangent = normalize((u_normalMatrix*a_tangent));
    //vec3 v_bitangent = cross(v_normal,v_tangent);
    vec3 v_bitangent = cross(v_tangent,v_normal);

    /*
      //actual world to tangent space
    v_tanToWorld = mat3(v_tangent[0],v_bitangent[0],v_normal[0],
                        v_tangent[1],v_bitangent[1],v_normal[1],
                        v_tangent[2],v_bitangent[2],v_normal[2]);
                        */

    v_tanToWorld = mat3(v_tangent,v_bitangent,v_normal);
}
