// GLSL AS C++ (MATERIAL-ENERGY-1 stage 1; shared with REFLECT-ESTIMATOR-1): the types and built-ins a SHIPPED piece of
// plain GLSL needs to compile as C++, so a CPU row evaluates the shipped text itself and never a copy of its formula.
//
// THE CHAIN, one text from the fork to the row (tests/support/glsl_as_cpp.cmake, jah_glsl_as_cpp()):
//   1. the ENGINE's own unwrap step (irisgl/cmake/WrapHlmsPiece.cmake -DUNWRAP=1, the one engine/CMakeLists.txt runs for
//      the ray jobs' glslang): the piece header and end dropped, any other directive mark refused;
//   2. TWO COUNTED REWRITES, each failing the build on a miscount (the only GLSL that is not C++ in the text):
//        `out float `   ->  `float & `          (a GLSL out parameter is a C++ reference)
//        `float[N](`    ->  `JAH_GLSL_ARRAY(`   (a GLSL array constructor; the macro below makes it a brace list)
//      and any other `out` / `inout` parameter refused;
//   3. the result #included INSIDE namespace jahglsl, after this header, so every call the text makes resolves to the
//      float built-ins below (unqualified lookup stops in the namespace: never the double overloads of <cmath>).
//
// WHAT IT IS NOT: a GPU. Float literals are double in C++ (float in GLSL), so a mixed expression evaluates in double and
// is rounded to float at the next float variable or call - at least as precise as the shader's fp32, never bit-equal to
// it. A row on this shim states laws at the 1e-4 scale or coarser; GPU-equals-text is a GPU fixture's (gi.lobe_parity).
//
// THE INCLUDER defines whatever the text's own includer would (e.g. JAH_SPEC_ALBEDO( k ), the table read) before the
// generated header. Header-only; one translation unit per program (the text's functions are not inline).
#pragma once

#include <cmath>

#define JAH_GLSL_ARRAY(...) { __VA_ARGS__ }

namespace jahglsl {

struct vec3 {
    float x, y, z;
    vec3() : x(0.0f), y(0.0f), z(0.0f) {}
    explicit vec3(float s) : x(s), y(s), z(s) {}
    vec3(float a, float b, float c) : x(a), y(b), z(c) {}
};
struct vec4 {
    float x, y, z, w;
    vec4() : x(0.0f), y(0.0f), z(0.0f), w(0.0f) {}
    explicit vec4(float s) : x(s), y(s), z(s), w(s) {}
    vec4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};

inline vec3 operator+(vec3 a, vec3 b) { return vec3(a.x + b.x, a.y + b.y, a.z + b.z); }
inline vec3 operator-(vec3 a, vec3 b) { return vec3(a.x - b.x, a.y - b.y, a.z - b.z); }
inline vec3 operator-(vec3 a) { return vec3(-a.x, -a.y, -a.z); }
inline vec3 operator*(vec3 a, vec3 b) { return vec3(a.x * b.x, a.y * b.y, a.z * b.z); }
inline vec3 operator*(vec3 a, float s) { return vec3(a.x * s, a.y * s, a.z * s); }
inline vec3 operator*(float s, vec3 a) { return a * s; }
inline vec3 operator/(vec3 a, float s) { return vec3(a.x / s, a.y / s, a.z / s); }

inline float dot(vec3 a, vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline vec3 cross(vec3 a, vec3 b) { return vec3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x); }
inline vec3 normalize(vec3 a) { return a * (1.0f / std::sqrt(dot(a, a))); }

inline float sqrt(float x) { return std::sqrt(x); }
inline float pow(float x, float y) { return std::pow(x, y); }
inline float asin(float x) { return std::asin(x); }
inline float min(float a, float b) { return b < a ? b : a; }
inline float max(float a, float b) { return a < b ? b : a; }
inline int min(int a, int b) { return b < a ? b : a; }
inline int max(int a, int b) { return a < b ? b : a; }
inline float clamp(float x, float lo, float hi) { return min(max(x, lo), hi); }
inline float mix(float a, float b, float t) { return a + (b - a) * t; }
inline vec3 mix(vec3 a, vec3 b, float t) { return a + (b - a) * t; }

}   // namespace jahglsl
