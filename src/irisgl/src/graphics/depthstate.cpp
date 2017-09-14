#include "depthstate.h"

namespace iris
{

DepthState DepthState::Default = DepthState(true, true);
DepthState DepthState::None = DepthState(false, false);

}
