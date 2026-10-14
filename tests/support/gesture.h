#pragma once
// enginetest::gesture — THE USER'S GESTURE, as a host submits it (ENGINE-UPGRADE-1 U0 3c, FrameInputScene::gesture).
// The engine no longer infers a drag from motion: a suite that drags a still object says so for each frame of the
// drag (the nodes the hand moves) and ends it with an empty one — exactly what the editor viewport sends.
#include <vector>
#include "jahshaka/engine/Engine.h"

namespace enginetest {

inline void gesture(jahshaka::engine::Engine *e, jahshaka::engine::Scene *s,
                    std::vector<jahshaka::engine::NodeId> nodes)
{
    jahshaka::engine::FrameInputGesture g;
    g.active = !nodes.empty();
    g.nodes = std::move(nodes);
    jahshaka::engine::FrameInputScene part;
    part.scene = s;
    part.gesture = g;
    jahshaka::engine::FrameInput in;
    in.scenes.push_back(part);
    e->submitFrame(in);
}

}   // namespace enginetest
