#include "blendstate.h"

namespace iris
{

BlendState BlendState::AlphaBlend = BlendState(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
BlendState BlendState::Opaque = BlendState(GL_ONE, GL_ZERO);
BlendState BlendState::Additive = BlendState(GL_ONE, GL_ONE);

}
