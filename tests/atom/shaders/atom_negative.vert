#version 450
// The graphics half's vertex stage: no attributes; the triangle the compute wrote.
layout( std430, set = 0, binding = 0 ) readonly buffer Data
{
	uint frame;
	uint pad0, pad1, pad2;
	vec4 tri[3];
} data;
layout( push_constant ) uniform Pc { uint frame; } pc;
void main()
{
	gl_Position = data.tri[gl_VertexIndex % 3];
}
