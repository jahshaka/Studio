#include "renderstates.h"

namespace iris
{

	RenderStates::RenderStates()
	{
		renderLayer = 0;
		blendState = BlendState::Opaque;
		depthState = DepthState::Default;
		rasterState = RasterizerState::CullCounterClockwise;

		fogEnabled = true;
		castShadows = true;
		receiveShadows = true;
		receiveLighting = true;
	}
}
