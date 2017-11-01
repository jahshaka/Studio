// https://github.com/NVIDIAGameWorks/GraphicsSamples/blob/master/samples/es3-kepler/FXAA/assets/shaders/FXAA_Default.frag

#version 150

#define FXAA_PC 1
#define FXAA_GLSL_130 1
#define FXAA_QUALITY__PRESET 39

// required for textureGather and textureGatherOffset
#extension GL_ARB_gpu_shader5 : enable

#pragma include "fxaa3_11.h"

in vec2 v_texCoord;

uniform sampler2D u_screenTex;
uniform vec2 u_screenSize;

out vec4 fragColor;
void main()
{
	vec4 color = texture(u_screenTex, v_texCoord);
        vec4 outputColor = FxaaPixelShader(v_texCoord,
                FxaaFloat4(0.0f, 0.0f, 0.0f, 0.0f),	//console
        u_screenTex,							// source texture
        u_screenTex,							// console
        u_screenTex,							// console
        u_screenSize,					// console
        FxaaFloat4(0.0f, 0.0f, 0.0f, 0.0f),	// console
        FxaaFloat4(0.0f, 0.0f, 0.0f, 0.0f),	// console
        FxaaFloat4(0.0f, 0.0f, 0.0f, 0.0f),	// console
        0.75f,								// fxaaQualitySubpix,
        0.166f,								// fxaaQualityEdgeThreshold,
        0.0833f,							// fxaaQualityEdgeThresholdMin,
        0.0f,								// console,
        0.0f,								// console,
        0.0f,								// console,
        FxaaFloat4(0.0f, 0.0f, 0.0f, 0.0f));	// console

        fragColor = vec4(outputColor.rgb, 1);
}
