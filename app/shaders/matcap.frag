#version 150

uniform sampler2D u_matTexture;
uniform bool u_useMatTexture;

in vec3 v_worldPos;
in vec3 v_normal;
uniform vec3 u_eyePos;

out vec4 fragColor;

vec2 matcap(vec3 eye, vec3 normal) {
    vec3 reflected = reflect(eye, normal);
    float m = 2.8284271247461903 * sqrt( reflected.z+1.0 );
    return reflected.xy / m + 0.5;
}

void main() {
    vec3 e = normalize(v_worldPos - u_eyePos);
    vec3 n = normalize(v_normal);
    vec3 r = (reflect(e, n));
    float m = 2.0 * sqrt(r.x * r.x + r.y * r.y + (r.z + 1.0) * (r.z + 1.0));
    vec2 N = r.xy / m + 0.5;
    //vec2 N = matcap(e,n);
    vec3 base = texture2D( u_matTexture, N ).rgb;

    if (u_useMatTexture) {
        fragColor = vec4(base, 1.0);
    } else {
        fragColor = vec4(1, 1, 0, 0.1);
    }
}
