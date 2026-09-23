#version 450
layout( location = 0 ) out vec4 outColour;
layout( push_constant ) uniform Pc { uint frame; } pc;
void main()
{
	outColour = vec4( 1.0, float( pc.frame & 255u ) / 255.0, 0.2, 1.0 );
}
