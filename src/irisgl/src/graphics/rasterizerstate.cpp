#include "rasterizerstate.h"

namespace iris
{

RasterizerState RasterizerState::CullCounterClockwise = RasterizerState(CullMode::CullCounterClockwise, GL_FILL);
RasterizerState RasterizerState::CullClockwise = RasterizerState(CullMode::CullClockwise, GL_FILL);
RasterizerState RasterizerState::CullNone = RasterizerState(CullMode::None, GL_FILL);

}
